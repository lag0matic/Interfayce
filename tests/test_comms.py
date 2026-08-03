import threading
import time
import unittest

from interfayce.comms import CommsDictation


class FakeAudio:
    pass


class FakeTranscriber:
    def __init__(self, transcript="hello from vr"):
        self.transcript = transcript

    def transcribe(self, _audio):
        return self.transcript


class FakeOsc:
    def __init__(self):
        self.messages = []
        self.clears = 0

    def send_chatbox_message(self, text):
        self.messages.append(text)

    def clear_chatbox(self):
        self.clears += 1


class CommsDictationTests(unittest.TestCase):
    def test_continuously_sends_completed_utterances_until_stopped(self):
        release = threading.Event()
        calls = 0

        def capture(**_kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                return FakeAudio()
            release.wait(0.2)
            raise type("WaitTimeoutError", (Exception,), {})()

        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(), capture=capture, osc=osc)
        self.assertEqual(comms.toggle().state, "LISTENING")
        deadline = time.monotonic() + 1.0
        while (not osc.messages or comms.snapshot().state != "SENT") \
                and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(osc.messages, ["hello from vr"])
        self.assertEqual(comms.snapshot().state, "SENT")
        self.assertEqual(comms.toggle().state, "STOPPING")
        release.set()

    def test_clear_uses_dedicated_empty_message_operation(self):
        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(), osc=osc)
        snapshot = comms.clear()
        self.assertEqual(osc.clears, 1)
        self.assertEqual(snapshot.state, "CLEARED")

    def test_refuses_to_cross_route_while_other_voice_capture_owns_lock(self):
        lock = threading.Lock()
        lock.acquire()
        comms = CommsDictation(FakeTranscriber(), lock, osc=FakeOsc())
        comms.toggle()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state == "LISTENING" and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(comms.snapshot().state, "ERROR")
        self.assertIn("already active", comms.snapshot().transcript)
        lock.release()

    def test_transcript_is_bounded_to_vrchat_limit(self):
        release = threading.Event()
        sent = threading.Event()
        calls = 0

        def capture(**_kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                return FakeAudio()
            release.wait(0.2)
            raise type("WaitTimeoutError", (Exception,), {})()

        osc = FakeOsc()
        original_send = osc.send_chatbox_message
        def send(text):
            original_send(text)
            sent.set()
        osc.send_chatbox_message = send
        comms = CommsDictation(FakeTranscriber("x" * 200), threading.Lock(), capture=capture, osc=osc)
        comms.toggle()
        self.assertTrue(sent.wait(1.0))
        self.assertEqual(len(osc.messages[0]), 144)
        comms.toggle()
        release.set()


if __name__ == "__main__":
    unittest.main()
