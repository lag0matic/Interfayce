import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from interfayce.capture_cue import capture_with_cues
from interfayce.parakeet_stt import capture_microphone_until

class CaptureReadyTests(unittest.TestCase):
    def test_first_frames_are_read_while_start_cue_is_still_playing(self):
        reading = threading.Event()
        cue_started = threading.Event()
        stop = threading.Event()
        order = []
        class Mic:
            CHUNK = 1
            SAMPLE_RATE = 16000
            SAMPLE_WIDTH = 2
            def __enter__(self):
                order.append("open")
                self.stream = self
                return self
            def __exit__(self, *args): order.append("close")
            def read(self, size):
                self_outer.assertTrue(cue_started.wait(1))
                reading.set()
                stop.set()
                return b"first words"
        self_outer = self
        def cue(start):
            if start:
                order.append("cue")
                cue_started.set()
                self.assertTrue(reading.wait(1))
            else: order.append("end")
        sr = SimpleNamespace(Microphone=lambda **kw: Mic(), AudioData=lambda data,*a:data)
        with patch.dict("sys.modules", {"speech_recognition":sr}), patch("interfayce.parakeet_stt._configured_microphone_index", return_value=None):
            result = capture_with_cues(capture_microphone_until, stop, cue=cue)
        self.assertEqual(result, b"first words")
        self.assertEqual(order, ["open", "cue", "close", "end"])
