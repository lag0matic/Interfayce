import json
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch

from interfayce.app_info import APP_VERSION, check_for_update


class AppInfoTests(unittest.TestCase):
    def test_runtime_version_matches_release_source(self) -> None:
        expected = (Path(__file__).resolve().parents[1] / "VERSION").read_text(
            encoding="utf-8").strip()
        self.assertEqual(APP_VERSION, expected)

    @staticmethod
    def _response(payload: dict[str, object]) -> MagicMock:
        response = MagicMock()
        response.__enter__.return_value.read.return_value = json.dumps(payload).encode("utf-8")
        return response

    def test_newer_release_is_reported(self) -> None:
        with patch("interfayce.app_info.urlopen", return_value=self._response({
            "tag_name": "v9.2.0", "html_url": "https://example.test/release"
        })):
            result = check_for_update()
        self.assertEqual(result.state, "available")
        self.assertEqual(result.version, "9.2.0")
        self.assertEqual(result.url, "https://example.test/release")

    def test_current_release_is_reported(self) -> None:
        with patch("interfayce.app_info.urlopen", return_value=self._response({
            "tag_name": f"v{APP_VERSION}"
        })):
            result = check_for_update()
        self.assertEqual(result.state, "current")


if __name__ == "__main__":
    unittest.main()
