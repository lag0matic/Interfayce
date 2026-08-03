import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.settings import AppSettings, adjust_tts_volume, load_settings, save_settings, toggle_tts_mute


class SettingsTests(unittest.TestCase):
    def test_round_trip_and_clamping(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            saved = save_settings(AppSettings(tts_volume=1.5, tts_muted=True, tts_speed=0.1))
            self.assertEqual(saved, AppSettings(tts_volume=1.0, tts_muted=True, tts_speed=0.25))
            self.assertEqual(load_settings(), saved)

    def test_volume_adjust_and_mute(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            self.assertAlmostEqual(adjust_tts_volume(0.1).tts_volume, 0.95)
            self.assertTrue(toggle_tts_mute().tts_muted)

    def test_invalid_file_falls_back_safely(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            path = Path(os.environ["INTERFAYCE_SETTINGS_PATH"])
            path.write_text("not json", encoding="utf-8")
            self.assertEqual(load_settings(), AppSettings())


if __name__ == "__main__":
    unittest.main()
