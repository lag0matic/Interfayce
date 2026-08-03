"""Windows Global Media Transport Controls integration.

Spotify participates in this operating-system media-session layer, so basic
metadata and transport controls work without Spotify OAuth or a cloud service.
"""

from __future__ import annotations

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

    async def _spotify_session(self):
        manager = await _manager_type().request_async()
        for session in manager.get_sessions():
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

    async def current_art_bytes(self) -> bytes | None:
        """Return Spotify's current Windows media-session thumbnail, if supplied."""
        session = await self._spotify_session()
        if session is None:
            return None
        properties = await session.try_get_media_properties_async()
        thumbnail = properties.thumbnail
        if thumbnail is None:
            return None
        try:
            from winrt.windows.storage.streams import DataReader  # type: ignore[import-not-found]
        except ImportError:
            return None

        stream = await thumbnail.open_read_async()
        reader = DataReader(stream)
        await reader.load_async(stream.size)
        data = bytearray(stream.size)
        reader.read_bytes(data)
        return bytes(data)

    async def toggle_play_pause(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_toggle_play_pause_async()

    async def next_track(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_skip_next_async()

    async def previous_track(self) -> bool:
        session = await self._spotify_session()
        return False if session is None else await session.try_skip_previous_async()
