"""Constrained voice intents and deterministic Music command execution."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import re

from .windows_media import WindowsSpotifyMedia


class MusicIntentKind(str, Enum):
    TOGGLE_PLAYBACK = "toggle_playback"
    NEXT_TRACK = "next_track"
    PREVIOUS_TRACK = "previous_track"
    NOW_PLAYING = "now_playing"
    UNKNOWN = "unknown"


@dataclass(frozen=True, slots=True)
class MusicIntent:
    kind: MusicIntentKind
    transcript: str


@dataclass(frozen=True, slots=True)
class MusicCommandResult:
    succeeded: bool
    message: str


def _normalized_words(transcript: str) -> str:
    return " ".join(re.findall(r"[a-z0-9']+", transcript.casefold()))


def _contains_phrase(words: str, phrases: tuple[str, ...]) -> bool:
    padded = f" {words} "
    return any(f" {phrase} " in padded for phrase in phrases)


def parse_music_intent(transcript: str) -> MusicIntent:
    """Recognize only commands safe to execute without an LLM or Spotify OAuth."""

    words = _normalized_words(transcript)
    if not words:
        return MusicIntent(MusicIntentKind.UNKNOWN, transcript)
    if _contains_phrase(words, (
        "what's playing", "what is playing", "who is this", "what song is this",
        "current track", "now playing",
    )):
        return MusicIntent(MusicIntentKind.NOW_PLAYING, transcript)
    if _contains_phrase(words, (
        "next", "next song", "next track", "skip", "skip this", "skip track",
    )):
        return MusicIntent(MusicIntentKind.NEXT_TRACK, transcript)
    if _contains_phrase(words, (
        "previous", "previous song", "previous track", "go back", "last song",
    )):
        return MusicIntent(MusicIntentKind.PREVIOUS_TRACK, transcript)
    if words in {
        "play", "pause", "resume", "stop", "toggle", "toggle playback",
        "pause music", "resume music", "play music", "stop music",
        "pause the music", "resume the music", "play the music", "stop the music",
    }:
        return MusicIntent(MusicIntentKind.TOGGLE_PLAYBACK, transcript)
    return MusicIntent(MusicIntentKind.UNKNOWN, transcript)


async def execute_music_intent(
    intent: MusicIntent,
    media: WindowsSpotifyMedia | None = None,
) -> MusicCommandResult:
    spotify = media or WindowsSpotifyMedia()
    if intent.kind is MusicIntentKind.NEXT_TRACK:
        succeeded = await spotify.next_track()
        return MusicCommandResult(succeeded, "Skipped to the next track." if succeeded
                                  else "Spotify did not accept the next-track command.")
    if intent.kind is MusicIntentKind.PREVIOUS_TRACK:
        succeeded = await spotify.previous_track()
        return MusicCommandResult(succeeded, "Returned to the previous track." if succeeded
                                  else "Spotify did not accept the previous-track command.")
    if intent.kind is MusicIntentKind.TOGGLE_PLAYBACK:
        succeeded = await spotify.toggle_play_pause()
        return MusicCommandResult(succeeded, "Playback toggled." if succeeded
                                  else "Spotify did not accept the playback command.")
    if intent.kind is MusicIntentKind.NOW_PLAYING:
        track = await spotify.current_track()
        if track is None:
            return MusicCommandResult(False, "Spotify is not reporting an active track.")
        return MusicCommandResult(True, f"{track.title}, by {track.artist}.")
    return MusicCommandResult(False, "I did not recognize a safe Music command.")
