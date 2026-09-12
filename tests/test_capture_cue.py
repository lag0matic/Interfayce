import unittest

from interfayce.capture_cue import cue_pcm


class CaptureCueTests(unittest.TestCase):
    def test_cues_are_short_quiet_and_distinct(self) -> None:
        start = cue_pcm(True)
        stop = cue_pcm(False)

        self.assertEqual(len(start), 2_160 * 2)
        self.assertEqual(len(stop), 1_800 * 2)
        self.assertNotEqual(start, stop)
        start_samples = memoryview(start).cast("h")
        stop_samples = memoryview(stop).cast("h")
        self.assertLessEqual(max(abs(value) for value in start_samples), 2_786)
        self.assertLessEqual(max(abs(value) for value in stop_samples), 2_786)
        self.assertEqual(start_samples[0], 0)
        self.assertEqual(stop_samples[0], 0)


if __name__ == "__main__":
    unittest.main()
