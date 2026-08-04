import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.settings import (
    AppSettings, adjust_broadcast_gain, adjust_tts_volume, load_settings, save_settings,
    set_desktop_configuration, set_runtime_controls, set_spotify_client_id,
    settings_wire_text, toggle_tts_mute,
)


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
            set_spotify_client_id("client-id")
            self.assertAlmostEqual(adjust_tts_volume(0.1).tts_volume, 0.95)
            self.assertTrue(toggle_tts_mute().tts_muted)
            self.assertEqual(adjust_broadcast_gain(3.0).broadcast_gain_db, 15.0)
            self.assertEqual(load_settings().spotify_client_id, "client-id")

    def test_invalid_file_falls_back_safely(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            path = Path(os.environ["INTERFAYCE_SETTINGS_PATH"])
            path.write_text("not json", encoding="utf-8")
            self.assertEqual(load_settings(), AppSettings())

    def test_runtime_controls_round_trip_and_wire_format(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            saved = set_runtime_controls(
                tts_volume=0.42,
                tts_muted=True,
                stt_microphone="Beyond Microphone",
                haptic_strength=0.37,
            )
            self.assertEqual(saved.stt_microphone, "Beyond Microphone")
            self.assertAlmostEqual(saved.haptic_strength, 0.37)
            self.assertEqual(settings_wire_text(saved), "42\t1\t1.00\t0.37\t12.0")

    def test_complete_desktop_configuration_round_trip(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            saved = set_desktop_configuration(
                tts_volume=0.4, tts_muted=False, tts_speed=1.1,
                tts_endpoint="http://tts.example.test/v1/audio/speech/",
                tts_model="voice-model", tts_voice="voice-a", tts_output="Headset",
                stt_microphone="Microphone", haptic_strength=0.3,
                broadcast_gain_db=9, spotify_client_id="client-id",
                llm_enabled=True, llm_endpoint="https://llm.example.test/v1/",
                llm_model="chat-model", llm_reasoning_effort="low",
                llm_temperature=0.5,
                comms_shortcuts=(("BRB", "Be right back."), ("MUTED", "I am muted.")),
            )

            self.assertTrue(saved.llm_enabled)
            self.assertEqual(saved.tts_endpoint, "http://tts.example.test/v1/audio/speech")
            self.assertEqual(saved.llm_endpoint, "https://llm.example.test/v1")
            self.assertEqual(saved.comms_shortcuts[0], ("BRB", "Be right back."))
            self.assertEqual(len(saved.comms_shortcuts), 4)
            self.assertEqual(load_settings(), saved)

    def test_comms_shortcuts_are_bounded_and_sanitized(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SETTINGS_PATH": str(Path(directory) / "settings.json")
        }):
            saved = save_settings(AppSettings(comms_shortcuts=(
                ("  TOO   LONG LABEL  ", " hello\nthere "),
                ("", "disabled"),
            )))
            self.assertEqual(saved.comms_shortcuts[0], ("TOO LONG LAB", "hello there"))
            self.assertEqual(saved.comms_shortcuts[1], ("", "disabled"))
            self.assertEqual(len(saved.comms_shortcuts), 4)

    def test_fresh_install_has_no_personal_service_configuration(self) -> None:
        defaults = AppSettings()
        self.assertFalse(defaults.llm_enabled)
        self.assertEqual(defaults.llm_endpoint, "")
        self.assertEqual(defaults.llm_model, "")
        self.assertEqual(defaults.tts_endpoint, "")
        self.assertEqual(defaults.tts_output, "")


if __name__ == "__main__":
    unittest.main()
