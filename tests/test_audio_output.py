from types import SimpleNamespace
import unittest
from unittest.mock import patch

from interfayce.kokoro import configured_output_device_index


class OutputSelectionTests(unittest.TestCase):
    def select(self, configured, devices):
        audio = SimpleNamespace(
            get_device_count=lambda: len(devices),
            get_device_info_by_index=lambda i: dict(name=devices[i][0],
                hostApi=devices[i][1], maxOutputChannels=devices[i][2]),
            get_host_api_info_by_index=lambda i: {"type": i})
        with patch.dict("os.environ", {"INTERFAYCE_TTS_OUTPUT": configured}), \
                patch("interfayce.kokoro.load_settings",
                    return_value=SimpleNamespace(tts_output="")):
            return configured_output_device_index(audio)

    def test_full_saved_name_matches_truncated_mme_before_directsound(self):
        name = "Speakers (3- Beyond Audio Strap)"
        self.assertEqual(self.select(name, [
            (name, 1, 2), (name[:31], 2, 2), (name, 13, 2)]), 1)

    def test_exact_identity_beats_substring_on_another_device(self):
        self.assertEqual(self.select("Headset", [
            ("Other Headset", 2, 2), ("Headset", 1, 2)]), 1)

    def test_short_named_device_prefers_mme(self):
        self.assertEqual(self.select("Headset", [
            ("Headset", 1, 2), ("Headset", 2, 2)]), 1)

    def test_input_only_device_is_not_selected(self):
        self.assertEqual(self.select("Headset", [
            ("Headset", 2, 0), ("Headset", 1, 2)]), 1)

    def test_missing_mme_keeps_other_matching_backend(self):
        self.assertEqual(self.select("Headset", [("Headset", 1, 2)]), 0)

    def test_numeric_override_default_and_missing_preserved(self):
        self.assertEqual(self.select("25", []), 25)
        self.assertIsNone(self.select("", []))
        self.assertIsNone(self.select("Missing", [("Headset", 2, 2)]))

    def test_arbitrary_short_prefix_is_not_treated_as_truncated_name(self):
        self.assertIsNone(self.select("Headset Pro", [("Headset", 2, 2)]))
