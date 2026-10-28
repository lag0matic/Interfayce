import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from dataclasses import replace

import pyaudio
import pytest

from interfayce import ming_tts, kokoro
from interfayce.settings import AppSettings


class FakeAudio:
    """A real-time callback clock without touching the user's speakers."""
    def __init__(self):
        self.playing = threading.Event()
        self.opened = 0
        self.pcm = bytearray()
        self.closed = self.terminated = False
        self.active = False

    def open(self, **kwargs):
        self.opened += 1
        self.callback = kwargs['stream_callback']
        assert kwargs['rate'] == 24000 and kwargs['channels'] == 1
        return self

    def start_stream(self):
        self.active = True
        self.playing.set()
        def pump():
            while self.active:
                data, status = self.callback(None, 480, {}, 0)
                self.pcm.extend(data)
                if status != pyaudio.paContinue:
                    self.active = False
                    break
                time.sleep(.02)
        self.thread = threading.Thread(target=pump)
        self.thread.start()

    def is_active(self):
        return self.active

    def stop_stream(self):
        self.active = False
        self.thread.join(timeout=2)

    def close(self):
        self.closed = True

    def terminate(self):
        self.terminated = True


@pytest.fixture
def server():
    behavior = {}
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *args):
            pass
        def do_POST(self):
            behavior['request'] = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            behavior['path'] = self.path
            behavior['authorization'] = self.headers.get('Authorization')
            self.send_response(behavior.get('status', 200))
            self.send_header('Content-Type', behavior.get('type', 'audio/pcm'))
            self.send_header('Transfer-Encoding', 'chunked')
            self.send_header('Connection', 'close')
            self.end_headers()
            try:
                for chunk in behavior.get('chunks', []):
                    self.wfile.write(f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n')
                    self.wfile.flush()
                if behavior.get('after_initial'):
                    behavior['after_initial']()
                self.wfile.write(b'0\r\n\r\n')
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
    http = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=http.serve_forever, daemon=True)
    thread.start()
    yield behavior, f'http://127.0.0.1:{http.server_port}/v1'
    http.shutdown()
    http.server_close()
    thread.join()


def configure(monkeypatch, endpoint):
    settings = replace(AppSettings(), ming_endpoint=endpoint, tts_volume=1.0)
    monkeypatch.setattr(ming_tts, 'load_settings', lambda: settings)
    monkeypatch.setattr(ming_tts, 'headers', lambda: {'Authorization': 'Bearer test', 'Content-Type': 'application/json'})
    monkeypatch.setattr(kokoro, 'configured_output_device_index', lambda _: None)
    return settings


def test_stream_begins_before_eof_and_preserves_odd_chunks(server, monkeypatch):
    behavior, endpoint = server
    configure(monkeypatch, endpoint)
    audio = FakeAudio()
    pcm = b'\x12\x34' * 12000
    behavior['chunks'] = [pcm[:1], pcm[1:111], pcm[111:10000], pcm[10000:]]
    before_eof = []
    behavior['after_initial'] = lambda: before_eof.append(audio.playing.wait(2))
    ming_tts.stream_speech('[voice:soft] Hello. [chuckle]', audio_factory=lambda: audio)
    assert before_eof == [True]
    assert bytes(audio.pcm[:len(pcm)]) == pcm
    assert audio.opened == 1 and audio.closed and audio.terminated
    assert behavior['request']['input'] == '[voice:soft] Hello. [chuckle]'
    assert behavior['request']['response_format'] == 'pcm'
    assert behavior['request']['speed'] == 1.0
    assert behavior['path'] == '/v1/audio/speech'
    assert behavior['authorization'] == 'Bearer test'


@pytest.mark.parametrize('status,kind', [(503, 'application/json'), (200, 'application/json')])
def test_errors_never_open_audio(server, monkeypatch, status, kind):
    behavior, endpoint = server
    configure(monkeypatch, endpoint)
    behavior.update(status=status, type=kind)
    audio = FakeAudio()
    with pytest.raises(RuntimeError):
        ming_tts.stream_speech('Hello', audio_factory=lambda: audio)
    assert not audio.opened


def test_cancel_during_blocked_read_stops_player_and_reader(server, monkeypatch):
    behavior, endpoint = server
    configure(monkeypatch, endpoint)
    audio = FakeAudio()
    cancel = threading.Event()
    release = threading.Event()
    behavior['chunks'] = [b'\x12\x34' * 10000]
    behavior['after_initial'] = lambda: release.wait(4)
    task = threading.Thread(target=lambda: ming_tts.stream_speech('Hello', cancel=cancel, audio_factory=lambda: audio))
    task.start()
    try:
        assert audio.playing.wait(2)
        cancel.set()
        task.join(timeout=1)
        assert not task.is_alive()
        assert audio.closed and audio.terminated
        assert not any(t.name == 'MingPCMReader' for t in threading.enumerate())
    finally:
        release.set()
        task.join(timeout=5)


def test_bounded_buffer_releases_blocked_producer_on_cancel():
    buffer = ming_tts.PcmBuffer(capacity=10)
    stop = threading.Event()
    producer = threading.Thread(target=lambda: buffer.append(bytes(1000), stop))
    producer.start()
    time.sleep(.03)
    assert len(buffer.data) == 10
    stop.set()
    producer.join(timeout=1)
    assert not producer.is_alive()


def test_truncated_sample_is_rejected():
    buffer = ming_tts.PcmBuffer()
    buffer.append(b'\x01', threading.Event())
    buffer.finish()
    assert isinstance(buffer.error, ValueError)


def test_endpoint_normalization_and_kokoro_tag_removal():
    assert ming_tts.speech_url('http://host/v1/') == 'http://host/v1/audio/speech'
    assert ming_tts.speech_url('http://host/v1/audio/speech') == 'http://host/v1/audio/speech'
    assert kokoro.strip_delivery_tags('[voice:soft] Hello. [chuckle]') == 'Hello.'


def test_speech_requests_are_serialized_and_kokoro_remains_selectable(monkeypatch):
    settings = AppSettings()
    monkeypatch.setattr(kokoro, 'load_settings', lambda: settings)
    entered = threading.Event()
    release = threading.Event()
    calls = []
    def streaming(text, **kwargs):
        calls.append(text)
        entered.set()
        release.wait(2)
    monkeypatch.setattr(ming_tts, 'stream_speech', streaming)
    first = threading.Thread(target=lambda: kokoro.speak('first'))
    second = threading.Thread(target=lambda: kokoro.speak('second'))
    first.start()
    assert entered.wait(1)
    second.start()
    time.sleep(.05)
    assert calls == ['first']
    release.set()
    first.join(2)
    second.join(2)
    assert calls == ['first', 'second']
    settings = replace(settings, tts_provider='kokoro')
    monkeypatch.setattr(kokoro, 'synthesize', lambda text: b'wav')
    monkeypatch.setattr(kokoro, 'play_wav', lambda audio: calls.append(audio))
    kokoro.speak('fallback')
    assert calls[-1] == b'wav'


def test_provider_round_trip_keeps_kokoro_configuration(tmp_path, monkeypatch):
    from interfayce.settings import load_settings, save_settings
    monkeypatch.setenv('INTERFAYCE_SETTINGS_PATH', str(tmp_path / 'settings.json'))
    old = replace(AppSettings(), tts_endpoint='http://kokoro/v1/audio/speech',
                  tts_model='kokoro', tts_voice='af_heart', tts_output='Headset')
    save_settings(old)
    assert load_settings().tts_provider == 'ming'
    save_settings(replace(load_settings(), tts_provider='kokoro'))
    restored = load_settings()
    assert restored.tts_endpoint == old.tts_endpoint
    assert restored.tts_voice == 'af_heart' and restored.tts_output == 'Headset'
    assert restored.tts_provider == 'kokoro'
