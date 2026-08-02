"""One-shot, non-spammy song announcements for the VRChat chatbox."""

from __future__ import annotations

from dataclasses import dataclass
from time import monotonic

from .media import MediaTrack

MAX_CHATBOX_CHARACTERS = 144
MUSIC_NOTE = "♫"


def _clean(value: str) -> str:
    return " ".join(value.split())


@dataclass(frozen=True)
class SongAnnouncement:
    artist: str
    title: str
    track_id: str | None = None

    @property
    def key(self) -> str:
        """Stable identity used to prevent duplicate announcements."""
        if self.track_id:
            return self.track_id
        return f"{_clean(self.artist).casefold()}\x1f{_clean(self.title).casefold()}"

    def chatbox_text(self) -> str:
        """Return the desired message, shortened cleanly to VRChat's limit."""
        artist = _clean(self.artist)
        title = _clean(self.title)
        if not artist or not title:
            raise ValueError("Both artist and title are required.")

        prefix = f"{MUSIC_NOTE} {artist} — "
        available_title_length = MAX_CHATBOX_CHARACTERS - len(prefix)
        if available_title_length <= 0:
            return f"{MUSIC_NOTE} {artist}"[:MAX_CHATBOX_CHARACTERS].rstrip()
        if len(title) <= available_title_length:
            return prefix + title
        if available_title_length == 1:
            return prefix + "…"
        return prefix + title[: available_title_length - 1].rstrip() + "…"


class SongAnnouncementGate:
    """Remembers the last announced song; skips are never broadcast twice."""

    def __init__(self) -> None:
        self._last_announced_key: str | None = None

    def should_announce(self, song: SongAnnouncement) -> bool:
        if song.key == self._last_announced_key:
            return False
        self._last_announced_key = song.key
        return True


class StableSongChangeWatcher:
    """Reports stable song changes, never the song already playing at startup."""

    def __init__(self, *, stability_seconds: float = 3.0) -> None:
        self.stability_seconds = stability_seconds
        self._current_key: str | None = None
        self._candidate: MediaTrack | None = None
        self._candidate_started_at: float | None = None

    def observe(self, track: MediaTrack | None, *, now: float | None = None) -> SongAnnouncement | None:
        now = monotonic() if now is None else now
        if track is None:
            return None
        if self._current_key is None:
            self._current_key = track.key
            return None
        if track.key == self._current_key:
            self._candidate = None
            self._candidate_started_at = None
            return None
        if self._candidate is None or self._candidate.key != track.key:
            self._candidate = track
            self._candidate_started_at = now
            return None
        if now - self._candidate_started_at < self.stability_seconds:
            return None

        self._current_key = track.key
        self._candidate = None
        self._candidate_started_at = None
        return SongAnnouncement(artist=track.artist, title=track.title, track_id=track.key)
