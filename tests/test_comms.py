import threading
import time
import unittest

from interfayce.comms import CommsDictation


class FakeAudio:
    pass


class EmptyAudio:
    frame_data = b""


class FakeTranscriber:
    def __init__(self, transcript="hello from vr"):
        self.transcript = transcript

    def transcribe(self, _audio):
        return self.transcript


class FakeOsc:
    def __init__(self):
        self.messages = []
        self.clears = 0
        self.typing = []

    def send_chatbox_message(self, text):
        self.messages.append(text)

    def clear_chatbox(self):
        self.clears += 1

    def set_typing(self, is_typing):
        self.typing.append(is_typing)


class CommsDictationTests(unittest.TestCase):
    def test_records_until_release_then_sends_one_complete_message(self):
        capture_started = threading.Event()
        capture_finished = threading.Event()

        def capture(stop_event, **_kwargs):
            capture_started.set()
            stop_event.wait(1.0)
            capture_finished.set()
            return FakeAudio()

        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(), capture=capture, osc=osc)
        self.assertEqual(comms.start().state, "LISTENING")
        self.assertTrue(capture_started.wait(1.0))
        self.assertEqual(osc.typing, [True])
        self.assertEqual(osc.messages, [])
        self.assertEqual(comms.stop().state, "TRANSCRIBING")
        self.assertFalse(osc.typing[-1])
        self.assertTrue(capture_finished.wait(1.0))
        deadline = time.monotonic() + 1.0
        while not osc.messages and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(osc.messages, ["hello from vr"])
        self.assertEqual(comms.snapshot().state, "SENT")
        self.assertFalse(osc.typing[-1])

    def test_start_is_idempotent_while_button_is_held(self):
        captures = 0

        def capture(stop_event, **_kwargs):
            nonlocal captures
            captures += 1
            stop_event.wait(1.0)
            return FakeAudio()

        comms = CommsDictation(FakeTranscriber(), threading.Lock(), capture=capture, osc=FakeOsc())
        self.assertEqual(comms.start().state, "LISTENING")
        self.assertEqual(comms.start().state, "LISTENING")
        comms.stop()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state not in {"SENT", "ERROR"} and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(captures, 1)

    def test_clear_uses_dedicated_empty_message_operation(self):
        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(), osc=osc)
        snapshot = comms.clear()
        self.assertEqual(osc.clears, 1)
        self.assertEqual(snapshot.state, "CLEARED")

    def test_shortcut_sends_bounded_normalized_message(self):
        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(), osc=osc)
        snapshot = comms.send_shortcut("  Be   right back.  ")
        self.assertEqual(osc.messages, ["Be right back."])
        self.assertEqual(snapshot.state, "SHORTCUT")
        self.assertEqual(snapshot.transcript, "Be right back.")

    def test_refuses_to_cross_route_while_other_voice_capture_owns_lock(self):
        lock = threading.Lock()
        lock.acquire()
        comms = CommsDictation(FakeTranscriber(), lock, osc=FakeOsc())
        comms.start()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state == "LISTENING" and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(comms.snapshot().state, "ERROR")
        self.assertIn("already active", comms.snapshot().transcript)
        lock.release()

    def test_transcript_is_bounded_to_vrchat_limit(self):
        sent = threading.Event()

        def capture(stop_event, **_kwargs):
            stop_event.wait(1.0)
            return FakeAudio()

        osc = FakeOsc()
        original_send = osc.send_chatbox_message
        def send(text):
            original_send(text)
            sent.set()
        osc.send_chatbox_message = send
        comms = CommsDictation(FakeTranscriber("x" * 200), threading.Lock(),
                               capture=capture, osc=osc)
        comms.start()
        comms.stop()
        self.assertTrue(sent.wait(1.0))
        self.assertEqual(len(osc.messages[0]), 144)
        self.assertTrue(osc.messages[0].endswith("…"))

    def test_long_transcript_ends_at_a_word_boundary(self):
        def capture(stop_event, **_kwargs):
            stop_event.wait(1.0)
            return FakeAudio()

        text = " ".join(["ordinary"] * 30)
        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(text), threading.Lock(),
                               capture=capture, osc=osc)
        comms.start()
        comms.stop()
        deadline = time.monotonic() + 1.0
        while not osc.messages and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertLessEqual(len(osc.messages[0]), 144)
        self.assertTrue(osc.messages[0].endswith("ordinary…"))

    def test_capture_has_no_vad_and_uses_release_event(self):
        observed = {}

        def capture(stop_event, **kwargs):
            observed.update(kwargs)
            observed["stop_event"] = stop_event
            stop_event.wait(1.0)
            return FakeAudio()

        comms = CommsDictation(FakeTranscriber(), threading.Lock(), capture=capture,
                               osc=FakeOsc())
        comms.start()
        deadline = time.monotonic() + 1.0
        while not observed and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(observed["max_seconds"], 30.0)
        self.assertFalse(observed["stop_event"].is_set())
        comms.stop()

    def test_capture_cues_bracket_the_recorded_interval(self):
        order = []

        def cue(starting):
            order.append("start-cue" if starting else "stop-cue")

        def capture(_stop_event, **_kwargs):
            order.append("capture")
            _kwargs["on_ready"]()
            return EmptyAudio()

        comms = CommsDictation(FakeTranscriber(), threading.Lock(), capture=capture,
                               osc=FakeOsc(), cue=cue)
        comms.start()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state == "LISTENING" and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(order, ["capture", "start-cue", "stop-cue"])

    def test_empty_tap_does_not_call_stt_or_send_osc(self):
        class UnexpectedTranscriber:
            def transcribe(self, _audio):
                raise AssertionError("empty audio must not reach STT")

        def capture(_stop_event, **_kwargs):
            return EmptyAudio()

        osc = FakeOsc()
        comms = CommsDictation(UnexpectedTranscriber(), threading.Lock(),
                               capture=capture, osc=osc)
        comms.start()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state == "LISTENING" and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(comms.snapshot().state, "IDLE")
        self.assertEqual(osc.messages, [])
        self.assertFalse(osc.typing[-1])

    def test_capture_failure_always_clears_typing_presence(self):
        def capture(_stop_event, **_kwargs):
            raise RuntimeError("microphone disappeared")

        osc = FakeOsc()
        comms = CommsDictation(FakeTranscriber(), threading.Lock(),
                               capture=capture, osc=osc)
        comms.start()
        deadline = time.monotonic() + 1.0
        while comms.snapshot().state == "LISTENING" and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertEqual(comms.snapshot().state, "ERROR")
        self.assertEqual(osc.typing, [True, False])


if __name__ == "__main__":
    unittest.main()
