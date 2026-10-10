import threading
import time
from interfayce.keyboard_dictation import KeyboardDictation

class Audio:
    frame_data = b'voice'
class Transcriber:
    def start_stream(self, callback): raise NotImplementedError
    def transcribe(self, audio): return ' A full Discord reply. <unk> '

def wait_finished(service, token):
    deadline = time.monotonic() + 2
    while service.command_lock.locked() and time.monotonic() < deadline:
        time.sleep(.002)
    assert not service.command_lock.locked()
    return service.status(token)

def capture(stop, **kwargs):
    stop.wait(1)
    return Audio()

def test_stop_returns_clean_full_text_and_releases_lock():
    service = KeyboardDictation(Transcriber(), threading.Lock(), capture=capture)
    token = service.start()
    assert token and service.start() is None
    assert service.control('stale') is False
    assert service.control(token)
    assert wait_finished(service, token) == 'DONE\tA full Discord reply.'

def test_shared_voice_lock_prevents_overlapping_capture():
    lock = threading.Lock()
    lock.acquire()
    service = KeyboardDictation(Transcriber(), lock, capture=capture)
    assert service.start() is None
    lock.release()

def test_cancel_discards_late_result():
    entered, release = threading.Event(), threading.Event()
    class Slow(Transcriber):
        def transcribe(self, audio):
            entered.set()
            release.wait(1)
            return 'must not insert'
    service = KeyboardDictation(Slow(), threading.Lock(), capture=lambda *a, **k: Audio())
    token = service.start()
    assert entered.wait(1)
    service.control(token, cancel=True)
    release.set()
    assert wait_finished(service, token) == 'CANCELLED\t'

def test_live_backend_final_used_and_closed():
    class Stream:
        closed = False
        def push(self, *a): pass
        def finish(self): return 'word ' * 100
        def close(self): self.closed = True
    stream = Stream()
    class Live(Transcriber):
        def start_stream(self, callback): return stream
        def transcribe(self, audio): raise AssertionError('must use live final')
    def capture_live(stop, **kwargs):
        assert kwargs['max_seconds'] == 30
        assert kwargs['on_chunk'] == stream.push
        return Audio()
    service = KeyboardDictation(Live(), threading.Lock(), capture=capture_live)
    token = service.start()
    assert wait_finished(service, token) == 'DONE\t' + ('word ' * 100).strip()
    assert stream.closed

def test_failed_live_final_falls_back():
    class Stream:
        def push(self, *a): pass
        def finish(self): raise RuntimeError('offline')
        def close(self): pass
    class Live(Transcriber):
        def start_stream(self, callback): return Stream()
    service = KeyboardDictation(Live(), threading.Lock(), capture=lambda *a, **k: Audio())
    token = service.start()
    assert wait_finished(service, token) == 'DONE\tA full Discord reply.'

def test_empty_capture_and_failure_release_lock():
    class Empty:
        frame_data = b''
    service = KeyboardDictation(Transcriber(), threading.Lock(), capture=lambda *a, **k: Empty())
    token = service.start()
    assert wait_finished(service, token) == 'DONE\t'
    def fail(*a, **k): raise RuntimeError('mic gone')
    service.capture = fail
    next_token = service.start()
    assert next_token != token
    assert service.status(token).startswith('ERROR')
    assert wait_finished(service, next_token).startswith('ERROR')
