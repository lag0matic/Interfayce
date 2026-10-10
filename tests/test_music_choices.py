import json
from unittest.mock import MagicMock

import pytest

from interfayce.llm_client import LlmResponse
from interfayce.music_choices import MusicChoices, ordinal_choice
from interfayce.music_conversation import run_music_request
from interfayce.pear_conversation import run_pear_request


def client(*actions):
    result = MagicMock()
    result.chat_json.side_effect = [LlmResponse(json.dumps(a)) for a in actions]
    return result


def spotify():
    api = MagicMock()
    api.playback_state.return_value = {}
    api.devices.return_value = [{"id": "device", "is_active": True}]
    api.search.return_value = {"tracks": {"items": [
        {"name": "Original", "uri": "spotify:track:original", "artists": [{"name": "Feint"}]},
        {"name": "Remix", "uri": "spotify:track:remix", "artists": [{"name": "Other"}]}]}}
    return api


def youtube():
    api = MagicMock()
    api.song.return_value = {}
    api.search.return_value = [
        {"title": "Original", "videoId": "original123", "detail": "Feint"},
        {"title": "Remix", "videoId": "remix123456", "detail": "Other"}]
    return api


@pytest.mark.parametrize("provider,run,factory", [
    ("spotify", run_music_request, spotify), ("youtube", run_pear_request, youtube)])
def test_followup_uses_spoken_order_not_search_order(provider, run, factory):
    memory, api = MusicChoices(), factory()
    first = run("play that song", choices=memory, api=api, client=client(
        {"tool": "search", "query": "song", "type": "track"},
        {"tool": "offer", "indices": [1, 0]}))
    assert first.message.index("Remix") < first.message.index("Original")
    followup_client = client()
    result = run("the second one please", choices=memory, api=api, client=followup_client)
    assert result.succeeded
    assert "Original" in result.message
    followup_client.chat_json.assert_not_called()
    assert api.search.call_count == 1
    if provider == "spotify":
        api.start_playback.assert_called_once_with(uri="spotify:track:original", device_id="device")
    else:
        api.play_video.assert_called_once_with("original123")
    assert memory.get(provider) == []


@pytest.mark.parametrize("provider,run,factory", [
    ("spotify", run_music_request, spotify), ("youtube", run_pear_request, youtube)])
def test_descriptive_followup_receives_real_pending_choices(provider, run, factory):
    memory, api = MusicChoices(), factory()
    run("play song", choices=memory, api=api, client=client(
        {"tool": "search", "query": "song", "type": "track"},
        {"tool": "offer", "indices": [0, 1]}))
    followup = client({"tool": "select", "index": 0})
    assert run("the one by Feint", choices=memory, api=api, client=followup).succeeded
    payload = json.loads(followup.chat_json.call_args.kwargs["user"])
    assert "Feint" in json.dumps(payload["observations"][0]["pending_choices"])


def test_expiry_and_provider_isolation():
    now = [0]
    memory = MusicChoices(clock=lambda: now[0])
    memory.save("spotify", ["one", "two"])
    assert memory.get("youtube") == []
    copy = memory.get("spotify")
    copy.clear()
    assert memory.get("spotify") == ["one", "two"]
    now[0] = 300
    assert memory.get("spotify") == []


@pytest.mark.parametrize("run,factory,provider", [
    (run_music_request, spotify, "spotify"), (run_pear_request, youtube, "youtube")])
def test_new_search_discards_pending_choices(run, factory, provider):
    memory, api = MusicChoices(), factory()
    run("play song", choices=memory, api=api, client=client(
        {"tool": "search", "query": "song", "type": "track"},
        {"tool": "offer", "indices": [0, 1]}))
    other = client({"tool": "search", "query": "unrelated", "type": "track"},
                   {"tool": "reply", "message": "Try another request."})
    run("play something unrelated", choices=memory, api=api, client=other)
    assert memory.get(provider) == []
    payload = json.loads(other.chat_json.call_args.kwargs["user"])
    assert not any("pending_choices" in row for row in payload["observations"])


def test_failed_dispatch_consumes_choices_without_retry():
    memory, api = MusicChoices(), spotify()
    run_music_request("play song", choices=memory, api=api, client=client(
        {"tool": "search", "query": "song", "type": "track"},
        {"tool": "offer", "indices": [0, 1]}))
    api.start_playback.side_effect = ValueError("unreadable response")
    assert not run_music_request("second", choices=memory, api=api, client=client()).succeeded
    api.start_playback.assert_called_once()
    assert memory.get("spotify") == []


@pytest.mark.parametrize("run,factory", [(run_music_request, spotify), (run_pear_request, youtube)])
def test_missing_choices_cannot_be_recreated_from_old_history(run, factory):
    llm, api = client(), factory()
    result = run("second one", choices=MusicChoices(), api=api, client=llm,
                 context=[{"response": "One: Original; Two: Remix"}])
    assert not result.succeeded
    llm.chat_json.assert_not_called()
    api.search.assert_not_called()


def test_ordinal_parser_does_not_swallow_song_titles():
    assert ordinal_choice("play Second Chance", 5) is None
    assert ordinal_choice("not the second one", 5) is None
    assert ordinal_choice("the third one", 2) is None
