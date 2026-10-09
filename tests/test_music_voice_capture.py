import threading
import unittest

from interfayce.music_voice_capture import MusicVoiceCapture


class MusicVoiceCaptureTests(unittest.TestCase):
    def test_release_before_worker_capture_is_not_lost(self):
        enter_capture = threading.Event()

        def command(*, stop_event):
            self.assertTrue(enter_capture.wait(1))
            return "released" if stop_event.is_set() else "still recording"

        capture = MusicVoiceCapture(command)
        token = capture.start()
        self.assertTrue(capture.stop(token))
        enter_capture.set()
        self.assertEqual(capture.result(token), "released")

    def test_duplicate_press_and_stale_release_cannot_affect_new_capture(self):
        def command(*, stop_event):
            return "released" if stop_event.wait(2) else "timed out"

        capture = MusicVoiceCapture(command)
        first = capture.start()
        self.assertIsNone(capture.start())
        self.assertTrue(capture.stop(first))
        self.assertEqual(capture.result(first), "released")
        second = capture.start()
        self.assertNotEqual(first, second)
        self.assertFalse(capture.stop(first))
        self.assertIsNone(capture.start())
        self.assertTrue(capture.stop(second))
        self.assertEqual(capture.result(second), "released")

    def test_worker_failure_releases_session(self):
        def command(*, stop_event):
            raise RuntimeError("capture failed")

        capture = MusicVoiceCapture(command)
        token = capture.start()
        self.assertTrue(capture.result(token).startswith("ERROR\t"))
        self.assertIsNotNone(capture.start())

    def test_unknown_session_does_not_wait(self):
        capture = MusicVoiceCapture(lambda **_: "OK")
        self.assertFalse(capture.stop("missing"))
        self.assertTrue(capture.result("missing").startswith("ERROR\t"))
