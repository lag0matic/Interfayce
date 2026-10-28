"""Windows Global Media Transport Controls integration.

Spotify participates in this operating-system media-session layer, so basic
metadata and transport controls work without Spotify OAuth or a cloud service.
"""

from __future__ import annotations

import asyncio
from concurrent.futures import Future
import threading
import time

from .media import MediaTrack


class WindowsMediaUnavailableError(RuntimeError):
    """Raised when the Windows media-session bridge is not installed/available."""


def _manager_type():
    try:
        from winrt.windows.media.control import (  # type: ignore[import-not-found]
            GlobalSystemMediaTransportControlsSessionManager,
        )
    except ImportError as error:
        raise WindowsMediaUnavailableError(
            "Windows media-session support is unavailable. Install this project's dependencies."
        ) from error
    return GlobalSystemMediaTransportControlsSessionManager


def _is_spotify(session: object) -> bool:
    source_id = getattr(session, "source_app_user_model_id", "")
    return "spotify" in source_id.casefold()


def _is_playing_status(status: object) -> bool:
    name = getattr(status, "name", "")
    if str(name).casefold() == "playing":
        return True
    try:
        return int(status) == 4  # Windows PlaybackStatus.Playing
    except (TypeError, ValueError):
        return False


class WindowsSpotifyMedia:
    """Finds Spotify's current Windows media session when it exists."""

    def __init__(self):
        self._lock = threading.Lock()
        self._manager_future = None
        self._art_cache = None

    async def _spotify_session(self):
        # Calls arrive on separate request threads/event loops. Share one agile
        # WinRT manager, using a cross-thread Future rather than an asyncio lock.
        with self._lock:
            pending = self._manager_future
            create = pending is None
            if create:
                pending = self._manager_future = Future()
        if create:
            try:
                pending.set_result(await _manager_type().request_async())
            except BaseException as error:
                pending.set_exception(error)
                with self._lock:
                    if self._manager_future is pending:
                        self._manager_future = None
                raise
        manager = await asyncio.shield(asyncio.wrap_future(pending))
        try:
            sessions = manager.get_sessions()
        except Exception:
            with self._lock:
                if self._manager_future is pending:
                    self._manager_future = None
                self._art_cache = None
            raise
        for session in sessions:
            if _is_spotify(session):
                return session
        return None

    async def current_track(self) -> MediaTrack | None:
        track, _playing = await self.current_track_and_playback()
        return track

    async def current_track_and_playback(self) -> tuple[MediaTrack | None, bool]:
        session = await self._spotify_session()
        if session is None:
            return None, False

        properties = await session.try_get_media_properties_async()
        artist = properties.artist.strip()
        title = properties.title.strip()
        if not artist or not title:
            return None, False
        track = MediaTrack(
            artist=artist,
            title=title,
            source_id=session.source_app_user_model_id,
        )
        playback = session.get_playback_info().playback_status
        return track, _is_playing_status(playback)

    async def current_snapshot(self):
        session = await self._spotify_session()
        if session is None:
            return None, False, b""
        properties = await session.try_get_media_properties_async()
        artist, title = properties.artist.strip(), properties.title.strip()
        if not artist or not title:
            return None, False, b""
        track = MediaTrack(artist=artist, title=title,
                           source_id=session.source_app_user_model_id)
        playing = _is_playing_status(session.get_playback_info().playback_status)
        try:
            art = await self._cached_art(track.key, properties)
        except Exception:
            art = b""  # Missing artwork must not hide valid track metadata.
        return track, playing, art or b""

    async def current_art_bytes(self) -> bytes | None:
        """Return Spotify's current Windows media-session thumbnail, if supplied."""
        session = await self._spotify_session()
        if session is None:
            return None
        properties = await session.try_get_media_properties_async()
        key = MediaTrack(properties.artist.strip(), properties.title.strip(),
                         session.source_app_user_model_id).key
        return await self._cached_art(key, properties)

    async def _cached_art(self, key, properties):
        with self._lock:
            cached = self._art_cache
        if cached and cached[0] == key and time.monotonic() < cached[1]:
            return cached[2]
        art = await self._art_bytes(properties)
        # Recheck missing art promptly; periodically refresh successful art in
        # case a player supplies a newer image for the same title.
        with self._lock:
            self._art_cache = (key, time.monotonic() + (30 if art else 2), art)
        return art

    @staticmethod
    async def _art_bytes(properties):
        thumbnail = properties.thumbnail
        if thumbnail is None:
            return None
        try:
            from winrt.windows.storage.streams import DataReader  # type: ignore[import-not-found]
        except ImportError:
            return None

        stream = await thumbnail.open_read_async()
        reader = None
        try:
            reader = DataReader(stream)
            if stream.size > 8 * 1024 * 1024:
                return None
            await reader.load_async(stream.size)
            data = bytearray(stream.size)
            reader.read_bytes(data)
            return bytes(data)
        finally:
            if reader is not None:
                reader.close()  # Also closes the underlying stream.
            else:
                stream.close()

    async def toggle_play_pause(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_toggle_play_pause_async()

    async def play(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_play_async()

    async def pause(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_pause_async()

    async def next_track(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_skip_next_async()

    async def previous_track(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_skip_previous_async()
