"""One-shot, non-spammy song announcements for the VRChat chatbox."""

from __future__ import annotations

from dataclasses import dataclass
import threading
from time import monotonic
from typing import Callable

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


class ResidentSongAnnouncer:
    """Own the song watcher for the lifetime of the resident app service."""

    def __init__(self, read_track: Callable[[], MediaTrack | None],
                 send_message: Callable[[str], None], clear_message: Callable[[], None],
                 *, poll_seconds: float = 1.0, clear_seconds: float = 7.0,
                 stability_seconds: float = 3.0) -> None:
        self._read_track = read_track
        self._send_message = send_message
        self._clear_message = clear_message
        self._poll_seconds = poll_seconds
        self._clear_seconds = clear_seconds
        self._watcher = StableSongChangeWatcher(stability_seconds=stability_seconds)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._clear_timer: threading.Timer | None = None

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="InterfayceSongAnnouncer", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._clear_timer is not None:
            self._clear_timer.cancel()
            self._clear_timer = None
        thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=2.0)

    def _schedule_clear(self) -> None:
        if self._clear_timer is not None:
            self._clear_timer.cancel()
        self._clear_timer = threading.Timer(self._clear_seconds, self._clear_message)
        self._clear_timer.daemon = True
        self._clear_timer.start()

    def _run(self) -> None:
        while not self._stop.is_set():
            track = self._read_track()
            announcement = self._watcher.observe(track)
            if announcement is not None:
                self._send_message(announcement.chatbox_text())
                self._schedule_clear()
            self._stop.wait(self._poll_seconds)
