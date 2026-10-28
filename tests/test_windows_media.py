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

class MediaResourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_manager_reused_for_concurrent_polls_and_recovers(self):
        import asyncio
        from unittest.mock import AsyncMock, Mock, patch
        from interfayce.windows_media import WindowsSpotifyMedia
        media = WindowsSpotifyMedia()
        session = FakeSession('Spotify.exe')
        manager = Mock()
        manager.get_sessions.return_value = [session]
        factory = Mock(request_async=AsyncMock(return_value=manager))
        with patch('interfayce.windows_media._manager_type', return_value=factory):
            results = await asyncio.gather(*(media._spotify_session() for _ in range(100)))
            self.assertTrue(all(item is session for item in results))
            self.assertEqual(factory.request_async.await_count, 1)
            manager.get_sessions.side_effect = OSError('broker restarted')
            with self.assertRaises(OSError):
                await media._spotify_session()
            manager.get_sessions.side_effect = None
            self.assertIs(await media._spotify_session(), session)
            self.assertEqual(factory.request_async.await_count, 2)

    async def test_art_cache_refreshes_for_track_change_and_expiry(self):
        from unittest.mock import AsyncMock, patch
        from interfayce.windows_media import WindowsSpotifyMedia
        media = WindowsSpotifyMedia()
        media._art_bytes = AsyncMock(side_effect=[b'first', b'second', b'updated'])
        with patch('interfayce.windows_media.time.monotonic', return_value=0):
            self.assertEqual(await media._cached_art('a', object()), b'first')
            self.assertEqual(await media._cached_art('a', object()), b'first')
            self.assertEqual(await media._cached_art('b', object()), b'second')
        with patch('interfayce.windows_media.time.monotonic', return_value=31):
            self.assertEqual(await media._cached_art('b', object()), b'updated')
        self.assertEqual(media._art_bytes.await_count, 3)

    async def test_art_reader_closed_on_success_and_decode_failure(self):
        from types import SimpleNamespace, ModuleType
        from unittest.mock import Mock, AsyncMock, patch
        from interfayce.windows_media import WindowsSpotifyMedia
        stream = Mock(size=4)
        reader = Mock(load_async=AsyncMock())
        module = ModuleType('winrt.windows.storage.streams')
        module.DataReader = Mock(return_value=reader)
        properties = SimpleNamespace(thumbnail=Mock(open_read_async=AsyncMock(return_value=stream)))
        with patch.dict('sys.modules', {'winrt.windows.storage.streams': module}):
            self.assertEqual(await WindowsSpotifyMedia._art_bytes(properties), bytes(4))
            reader.close.assert_called_once()
            reader.close.reset_mock()
            reader.load_async.side_effect = OSError('read failed')
            with self.assertRaises(OSError):
                await WindowsSpotifyMedia._art_bytes(properties)
            reader.close.assert_called_once()
            module.DataReader.side_effect = OSError('constructor failed')
            with self.assertRaises(OSError):
                await WindowsSpotifyMedia._art_bytes(properties)
            stream.close.assert_called_once()
