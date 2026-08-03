import unittest

from interfayce.windows_media import _is_playing_status, _is_spotify


class FakeSession:
    def __init__(self, source_id: str) -> None:
        self.source_app_user_model_id = source_id


class WindowsMediaTests(unittest.TestCase):
    def test_identifies_spotify_sessions(self) -> None:
        self.assertTrue(_is_spotify(FakeSession("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify")))

    def test_identifies_playing_status_by_enum_name_or_windows_value(self):
        named = type("Status", (), {"name": "Playing"})()
        self.assertTrue(_is_playing_status(named))
        self.assertTrue(_is_playing_status(4))
        self.assertFalse(_is_playing_status(5))
        self.assertTrue(_is_spotify(FakeSession("Spotify.exe")))
        self.assertFalse(_is_spotify(FakeSession("chrome.exe")))
