import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.app_info import APP_VERSION
from interfayce.diagnostics import (load_report, needs_first_run, run_diagnostics,
                                    save_report)
from interfayce.settings import AppSettings


class DiagnosticsTests(unittest.TestCase):
    def test_core_and_optional_results_are_distinct(self) -> None:
        report = run_diagnostics(
            AppSettings(), voice_service_ready=False,
            input_devices=[], output_devices=[], install_root=Path("not-installed"),
        )
        states = {result.name: result.state for result in report.results}
        self.assertEqual(states["Voice service"], "attention")
        self.assertEqual(states["Audio input"], "attention")
        self.assertEqual(states["Kokoro"], "optional")
        self.assertEqual(states["Spotify OAuth"], "optional")

    def test_ready_install_report_round_trips(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_DIAGNOSTICS_PATH": str(Path(directory) / "diagnostics.json")
        }):
            root = Path(directory) / "install"
            root.mkdir()
            for name in ("InterfayceOverlay.exe", "InterfayceAudioEngine.exe", "openvr_api.dll"):
                (root / name).touch()
            report = run_diagnostics(
                AppSettings(tts_endpoint="http://tts.test", spotify_client_id="client"),
                voice_service_ready=True, input_devices=["Mic"],
                output_devices=["CABLE Input"], install_root=root,
            )
            save_report(report)
            loaded = load_report()
        self.assertEqual(loaded, report)
        self.assertEqual(report.attention_count, 0)
        self.assertFalse(needs_first_run(report, installed=True))

    def test_old_version_needs_first_run(self) -> None:
        report = run_diagnostics(
            AppSettings(), voice_service_ready=True,
            input_devices=["Mic"], output_devices=["Speakers"],
        )
        self.assertEqual(report.version, APP_VERSION)
        self.assertFalse(needs_first_run(report))


if __name__ == "__main__":
    unittest.main()
