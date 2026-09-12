import threading
import time
from unittest.mock import patch

from interfayce.live_stt import clean_transcript, caption_tail, LiveCaptionPublisher, RemoteLiveSession
from interfayce.comms import CommsDictation


def test_garbage_filter_preserves_short_speech():
    assert clean_transcript("<unk> <|nospeech|> ...") == ""
    assert clean_transcript("<unk> Oh, mm! <blank>") == "Oh, mm!"
    assert clean_transcript("I like <3") == "I like <3"


def test_rolling_caption_keeps_recent_whole_words():
    text = " ".join("word" + str(i) for i in range(50))
    tail = caption_tail(text)
    assert len(tail) <= 144
    assert tail.endswith("word49")
    assert tail.startswith("…word")


def test_partial_corrections_settle_and_final_flushes():
    sent = []
    now = [0.]
    publisher = LiveCaptionPublisher(sent.append, lambda: now[0])
    publisher.update("We like cats")
    assert sent == []
    publisher.update("We like cuddles")
    assert sent == ["We like"]
    publisher.update("We like cuddles today")
    assert sent == ["We like"]
    now[0] = 1.1
    publisher.update("We like cuddles today")
    assert sent[-1] == "We like cuddles today"
    publisher.update("We like cuddles today.", final=True)
    assert sent[-1] == "We like cuddles today."


class Osc:
    def __init__(self):
        self.messages = []
    def send_chatbox_message(self, text):
        self.messages.append(text)
    def set_typing(self, value):
        pass
    def clear_chatbox(self):
        self.messages.append("<clear>")


def test_stream_shows_text_before_release_and_clear_suppresses_late_results():
    active = threading.Event()
    class Stream:
        def push(self, *args):
            pass
        def finish(self):
            return "hello world finished"
        def close(self):
            pass
    class Transcriber:
        def start_stream(self, callback):
            self.callback = callback
            return Stream()
        def transcribe(self, audio):
            raise AssertionError("Streaming succeeded")
    transcriber = Transcriber()
    def capture(stop, **kwargs):
        assert "on_chunk" in kwargs
        transcriber.callback("hello world")
        transcriber.callback("hello world again")
        active.set()
        stop.wait(2)
        return object()
    osc = Osc()
    comms = CommsDictation(transcriber, threading.Lock(), capture=capture, osc=osc)
    comms.start()
    assert active.wait(1)
    assert osc.messages == ["hello world"]
    comms.clear()
    comms.stop()
    comms._thread.join(1)
    assert osc.messages == ["hello world", "<clear>"]
    assert comms.snapshot().state == "CLEARED"


def test_failed_stream_reuses_complete_recording():
    audio = object()
    class Stream:
        def push(self, *args):
            pass
        def finish(self):
            raise OSError("server disconnected")
        def close(self):
            pass
    class Transcriber:
        def start_stream(self, callback):
            return Stream()
        def transcribe(self, recording):
            assert recording is audio
            return "<unk> still here"
    osc = Osc()
    comms = CommsDictation(Transcriber(), threading.Lock(),
        capture=lambda *a, **k: audio, osc=osc)
    comms.start()
    comms._thread.join(1)
    assert osc.messages == ["still here"]


def test_worker_resamples_in_order_and_sends_final_tail():
    requests = []
    def request(self, method, url, body=None):
        requests.append((method, url, body))
        if url == self.base:
            return {"session": "test"}
        return {"text": "hello"}
    with patch.object(RemoteLiveSession, "_request", request):
        session = RemoteLiveSession("http://localhost/v1/audio/transcriptions", {}, lambda t: None)
        for i in range(10):
            session.push(bytes(4410 * 2), 44100, 2)
        assert session.finish() == "hello"
    chunks = [r for r in requests if "?sequence" in r[1]]
    assert chunks[-1][1].endswith("final=1")
    assert abs(sum(len(r[2]) for r in chunks) - 32000) <= 2
    assert "sequence=0" in chunks[0][1]
