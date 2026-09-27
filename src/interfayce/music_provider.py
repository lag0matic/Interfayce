"""Provider selection shared by transport, artwork and song announcements."""
from .settings import load_settings
from .windows_media import WindowsSpotifyMedia
from .pear_music import PearMedia


class SelectedMusicMedia:
    def __init__(self):
        self.spotify = WindowsSpotifyMedia()
        self.youtube = PearMedia()

    def selected(self):
        return self.youtube if load_settings().music_provider == "youtube" else self.spotify

    async def current_track(self):
        return await self.selected().current_track()

    async def current_track_and_playback(self):
        return await self.selected().current_track_and_playback()

    async def current_art_bytes(self):
        return await self.selected().current_art_bytes()

    async def previous_track(self):
        return await self.selected().previous_track()

    async def next_track(self):
        return await self.selected().next_track()

    async def toggle_play_pause(self):
        return await self.selected().toggle_play_pause()

    async def play(self):
        return await self.selected().play()

    async def pause(self):
        return await self.selected().pause()
