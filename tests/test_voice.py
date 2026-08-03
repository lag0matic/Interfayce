import asyncio
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from interfayce.voice import (
    MusicIntentKind,
    execute_music_intent,
    parse_music_intent,
)
from interfayce.voice_service import voice_log_path


class FakeMedia:
    def __init__(self) -> None:
        self.called = ""

    async def next_track(self) -> bool:
        self.called = "next"
        return True

    async def previous_track(self) -> bool:
        self.called = "previous"
        return True

    async def toggle_play_pause(self) -> bool:
        self.called = "toggle"
        return True

    async def current_track(self):
        self.called = "current"
        return None


class VoiceIntentTests(unittest.TestCase):
    def test_recognizes_bounded_transport_commands(self) -> None:
        cases = {
            "pause music": MusicIntentKind.TOGGLE_PLAYBACK,
            "pause the music": MusicIntentKind.TOGGLE_PLAYBACK,
            "skip this": MusicIntentKind.NEXT_TRACK,
            "go back": MusicIntentKind.PREVIOUS_TRACK,
            "what song is this?": MusicIntentKind.NOW_PLAYING,
        }
        for transcript, expected in cases.items():
            with self.subTest(transcript=transcript):
                self.assertEqual(parse_music_intent(transcript).kind, expected)

    def test_rejects_unrecognized_or_search_commands(self) -> None:
        self.assertEqual(parse_music_intent("play some Bowie").kind, MusicIntentKind.UNKNOWN)
        self.assertEqual(parse_music_intent("open the nextdoor app").kind, MusicIntentKind.UNKNOWN)
        self.assertEqual(parse_music_intent("").kind, MusicIntentKind.UNKNOWN)

    def test_executes_only_parsed_command(self) -> None:
        media = FakeMedia()
        result = asyncio.run(execute_music_intent(parse_music_intent("next track"), media))
        self.assertTrue(result.succeeded)
        self.assertEqual(media.called, "next")

    def test_voice_log_path_is_configurable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            expected = Path(directory) / "voice.log"
            with patch.dict("os.environ", {"INTERFAYCE_VOICE_LOG": str(expected)}):
                self.assertEqual(voice_log_path(), expected)
