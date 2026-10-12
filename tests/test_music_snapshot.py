import asyncio
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from interfayce.windows_media import WindowsSpotifyMedia
from interfayce.pear_music import PearMedia
from interfayce.music_provider import SelectedMusicMedia


def test_spotify_snapshot_uses_same_properties_for_title_and_art():
    old = SimpleNamespace(artist='Artist', title='First song', thumbnail='first')
    new = SimpleNamespace(artist='Artist', title='Second song', thumbnail='second')
    session = SimpleNamespace(source_app_user_model_id='Spotify',
        try_get_media_properties_async=AsyncMock(side_effect=[old, new]),
        get_playback_info=lambda: SimpleNamespace(playback_status=4))
    media = WindowsSpotifyMedia()
    media._spotify_session = AsyncMock(return_value=session)
    media._art_bytes = AsyncMock(side_effect=lambda properties: properties.thumbnail.encode())
    track, playing, art = asyncio.run(media.current_snapshot())
    assert (track.title, playing, art) == ('First song', True, b'first')
    assert session.try_get_media_properties_async.await_count == 1


def test_pear_snapshot_only_reads_song_once():
    media = PearMedia()
    media.api.song = Mock(side_effect=[
        dict(title='First', artist='Artist', videoId='one', imageSrc='first', isPaused=False),
        dict(title='Second', artist='Artist', videoId='two', imageSrc='second')])
    media._song_art = Mock(side_effect=lambda song: song['imageSrc'].encode())
    track, playing, art = asyncio.run(media.current_snapshot())
    assert (track.title, playing, art) == ('First', True, b'first')
    assert media.api.song.call_count == 1


def test_provider_is_pinned_for_entire_snapshot():
    media = SelectedMusicMedia()
    media.spotify.current_snapshot = AsyncMock(return_value=('track', True, b'art'))
    with patch('interfayce.music_provider.load_settings', side_effect=[
            SimpleNamespace(music_provider='spotify'), SimpleNamespace(music_provider='youtube')]) as load:
        assert asyncio.run(media.current_snapshot()) == ('spotify', 'track', True, b'art')
        assert load.call_count == 1


def test_art_failure_keeps_track_metadata():
    media = PearMedia()
    media.api.song = Mock(return_value=dict(title='Song', artist='Artist', isPaused=False))
    media._song_art = Mock(side_effect=TimeoutError())
    track, playing, art = asyncio.run(media.current_snapshot())
    assert (track.title, playing, art) == ('Song', True, b'')


def test_wire_snapshot_preserves_binary_art_and_sanitizes_track_fields():
    from interfayce.voice_service import VoiceRuntime
    runtime = VoiceRuntime.__new__(VoiceRuntime)
    art = b'\x00\xff\t\ncover'
    track = SimpleNamespace(artist='Artist\tname', title='Song\nname')
    runtime._song_media = SimpleNamespace(current_snapshot=AsyncMock(
        return_value=('youtube', track, True, art)))
    header, received_art = runtime.music_snapshot().split(b'\n', 1)
    assert header == b'youtube\tPLAYING\tArtist name\tSong name'
    assert received_art == art
