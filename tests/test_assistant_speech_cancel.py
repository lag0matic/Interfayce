from io import BytesIO
from types import SimpleNamespace
import sys
import threading
import wave

from interfayce import kokoro


def test_speech_stops_between_audio_chunks(monkeypatch):
    cancel = threading.Event()
    written = []
    closed = []
    def write(frames):
        written.append(frames)
        cancel.set()
    stream = SimpleNamespace(write=write, stop_stream=lambda: None,
                             close=lambda: closed.append(True))
    audio = SimpleNamespace(get_default_output_device_info=lambda: {'name': 'test'},
                            get_format_from_width=lambda _: 0,
                            open=lambda **_: stream, terminate=lambda: None)
    monkeypatch.setitem(sys.modules, 'pyaudio', SimpleNamespace(PyAudio=lambda: audio))
    monkeypatch.setattr(kokoro, 'configured_output_device_index', lambda _: None)
    monkeypatch.setattr(kokoro, 'load_settings', lambda: SimpleNamespace(tts_muted=False, tts_volume=1.0))
    output = BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(24000)
        wav.writeframes(b'\0' * 24000)
    kokoro.play_wav(output.getvalue(), cancel=cancel)
    assert len(written) == 1
    assert closed == [True]
