import asyncio
import json
from dataclasses import replace
from unittest.mock import MagicMock, patch

import pytest

from interfayce.llm_client import LlmResponse
from interfayce.media import MediaTrack
from interfayce.music_provider import SelectedMusicMedia
from interfayce.pear_conversation import run_pear_request
from interfayce.pear_music import PearApi, PearError, PearMedia, queue_entries, search_tracks
from interfayce.settings import AppSettings, load_settings, save_settings

VIDEO = "wCq2sj1zBQg"


def queue(video=VIDEO, wrapped=False):
    row = {"playlistPanelVideoRenderer": {"videoId": video}}
    if wrapped:
        row = {"playlistPanelVideoWrapperRenderer": {"primaryRenderer": row}}
    return {"items": [row]}


def test_both_live_queue_shapes():
    assert queue_entries(queue()) == queue_entries(queue(wrapped=True))
    assert queue_entries({"items": [{"unavailable": True}]}) == []


def test_search_ignores_menu_links_and_nonplayable_results():
    row = {"musicResponsiveListItemRenderer": {"flexColumns": [
        {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [
            {"text": "Tech Noir", "navigationEndpoint": {"watchEndpoint": {"videoId": VIDEO}}}]}}}],
        "menu": {"watchEndpoint": {"videoId": "differentID"}}}}
    results = search_tracks({"contents": [row, row, {"watchEndpoint": {"videoId": "differentID"}}]})
    assert results == [{"videoId": VIDEO, "title": "Tech Noir", "detail": ""}]


def test_selection_reuses_queue_entry_and_confirms_playback():
    api = PearApi()
    api.request = MagicMock(side_effect=[queue(wrapped=True), None, None,
                                        {"videoId": VIDEO, "isPaused": False}])
    api.play_video(VIDEO)
    assert [(c.args[0], c.args[1] if len(c.args) > 1 else "GET") for c in api.request.call_args_list] == [
        ("queue", "GET"), ("queue", "PATCH"), ("play", "POST"), ("song", "GET")]


def test_uncertain_insertion_is_not_repeated_or_selected():
    api = PearApi()
    api.request = MagicMock(side_effect=[queue("otherID1234"), PearError("timed out")])
    with pytest.raises(PearError):
        api.play_video(VIDEO)
    assert api.request.call_count == 2


def test_unconfirmed_insertion_is_not_repeated():
    api = PearApi()
    api.request = MagicMock(return_value={"items": []})
    with patch("interfayce.pear_music.time.monotonic", side_effect=[0, 5]):
        with pytest.raises(PearError, match="haven't sent it again"):
            api.play_video(VIDEO)
    assert api.request.call_count == 2


def test_invalid_video_never_dispatches():
    api = PearApi()
    api.request = MagicMock()
    with pytest.raises(PearError):
        api.play_video("https://example.com")
    api.request.assert_not_called()


def test_no_song_and_paused_metadata():
    media = PearMedia()
    media.api.song = MagicMock(return_value={})
    assert asyncio.run(media.current_track_and_playback()) == (None, False)
    media.api.song.return_value = {"title": "Tech Noir", "artist": "GUNSHIP", "videoId": VIDEO, "isPaused": True}
    track, playing = asyncio.run(media.current_track_and_playback())
    assert track == MediaTrack("GUNSHIP", "Tech Noir", "youtube-music:" + VIDEO)
    assert playing is False


def test_provider_selection_updates_without_restarting():
    media = SelectedMusicMedia()
    with patch("interfayce.music_provider.load_settings", return_value=AppSettings()) as settings:
        assert media.selected() is media.spotify
        settings.return_value = replace(AppSettings(), music_provider="youtube")
        assert media.selected() is media.youtube


def test_provider_persists_and_invalid_value_defaults_to_spotify(tmp_path, monkeypatch):
    monkeypatch.setenv("INTERFAYCE_SETTINGS_PATH", str(tmp_path / "settings.json"))
    save_settings(replace(AppSettings(), music_provider="youtube"))
    assert load_settings().music_provider == "youtube"
    save_settings(replace(AppSettings(), music_provider="invalid"))
    assert load_settings().music_provider == "spotify"


def test_llm_selects_only_real_result_and_never_retries_playback():
    api, client = MagicMock(), MagicMock()
    api.song.return_value = {}
    api.search.return_value = [{"videoId": VIDEO, "title": "Tech Noir", "detail": "GUNSHIP"}]
    api.play_video.side_effect = PearError("Unconfirmed; not retried")
    client.chat_json.side_effect = [LlmResponse(json.dumps(action)) for action in (
        {"tool": "select", "index": 40},
        {"tool": "search", "query": "Gunship Tech Noir"},
        {"tool": "select", "index": 0})]
    result = run_pear_request("play Tech Noir", api=api, client=client)
    assert not result.succeeded
    api.play_video.assert_called_once_with(VIDEO)
    assert client.chat_json.call_count == 3


def test_artwork_rejects_nonimage_hosts():
    media = PearMedia()
    media.api.song = MagicMock(return_value={"imageSrc": "http://127.0.0.1/secret"})
    with patch("interfayce.pear_music.urlopen") as fetch:
        assert asyncio.run(media.current_art_bytes()) == b""
        fetch.assert_not_called()
