import json
import unittest
from unittest.mock import MagicMock
from interfayce.llm_client import LlmResponse
from interfayce.music_conversation import run_music_request
from interfayce.voice import parse_music_intent, MusicIntentKind

class MusicConversationTests(unittest.TestCase):
    def run_actions(self, actions, api=None):
        client = MagicMock()
        client.chat_json.side_effect = [LlmResponse(json.dumps(a)) for a in actions]
        api = api or self.api()
        return run_music_request("play something by them", client=client, api=api), client, api
    def api(self):
        api = MagicMock()
        api.devices.return_value = [{"id": "device", "is_active": True}]
        api.playback_state.return_value = {"device": {"id": "device"}, "item": {"name": "Current", "artists": [{"name": "Muse"}]}}
        api.search.return_value = {"tracks": {"items": [{"name": "Hysteria", "uri": "spotify:track:real", "artists": [{"name": "Muse"}]}]}}
        return api
    def test_search_then_select_uses_real_uri_and_playback_context(self):
        result, client, api = self.run_actions([
            {"tool":"search", "type":"track", "query":"Muse"},
            {"tool":"select", "index":0}])
        self.assertTrue(result.succeeded)
        api.start_playback.assert_called_once_with(uri="spotify:track:real", device_id="device")
        self.assertIn("Muse", client.chat_json.call_args_list[0].kwargs["user"])
    def test_invalid_candidate_cannot_execute_and_can_be_clarified(self):
        result, _, api = self.run_actions([
            {"tool":"select", "index":99},
            {"tool":"reply", "message":"Which artist did you mean?"}])
        self.assertFalse(result.succeeded)
        api.start_playback.assert_not_called()
    def test_malformed_json_recovers(self):
        client = MagicMock()
        client.chat_json.side_effect = [LlmResponse("oops"), LlmResponse('{"tool":"reply","message":"Which song?"}')]
        result = run_music_request("huh", client=client, api=self.api())
        self.assertEqual(result.message, "Which song?")
    def test_pause_is_pause_not_toggle(self):
        result, _, api = self.run_actions([{"tool":"control", "command":"pause", "value":None}])
        self.assertTrue(result.succeeded)
        api.pause.assert_called_once_with(device_id="device")
    def test_keyword_in_song_name_does_not_steal_request(self):
        for text in ("play Next to Me", "play Skip the Line", "don't skip this"):
            self.assertEqual(parse_music_intent(text).kind, MusicIntentKind.UNKNOWN)

    def test_named_track_preserves_album_context_for_next_and_previous(self):
        api = self.api()
        api.search.return_value["tracks"]["items"][0]["album"] = {"uri":"spotify:album:real"}
        result, _, api = self.run_actions([
            {"tool":"search", "type":"track", "query":"Muse"},
            {"tool":"select", "index":0}], api=api)
        self.assertTrue(result.succeeded)
        api.start_playback.assert_called_once_with(
            context_uri="spotify:album:real", offset_uri="spotify:track:real", device_id="device")
