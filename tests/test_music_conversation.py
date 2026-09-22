import json
import unittest
from unittest.mock import MagicMock
from interfayce.llm_client import LlmResponse
from interfayce.music_conversation import run_music_request
from interfayce.voice import parse_music_intent, MusicIntentKind

class MusicConversationTests(unittest.TestCase):
    def test_shuffle_failure_does_not_replay_or_report_missing_music(self):
        from interfayce.spotify_oauth import SpotifyOAuthError
        for failure in (ValueError("invalid response"), SpotifyOAuthError("shuffle unavailable")):
            with self.subTest(failure=type(failure).__name__):
                api = self.api()
                api.search.return_value = {"playlists": {"items": [
                    {"name": "Chillstep", "uri": "spotify:playlist:chill", "artists": None}]}}
                api.set_shuffle.side_effect = failure
                result, client, api = self.run_actions([
                    {"tool": "search", "type": "playlist", "query": "chillstep"},
                    {"tool": "select", "index": 0, "shuffle": True}], api=api)
                self.assertTrue(result.succeeded)
                self.assertEqual(result.message, "Playing Chillstep. I couldn't update shuffle.")
                api.start_playback.assert_called_once()
                self.assertEqual(client.chat_json.call_count, 2)

    def test_unreadable_playback_response_does_not_retry_command(self):
        api = self.api()
        api.start_playback.side_effect = ValueError("unreadable response after dispatch")
        result, client, api = self.run_actions([
            {"tool": "search", "type": "track", "query": "Muse"},
            {"tool": "select", "index": 0}], api=api)
        self.assertFalse(result.succeeded)
        self.assertIn("haven't retried", result.message)
        api.start_playback.assert_called_once()
        self.assertEqual(client.chat_json.call_count, 2)

    def test_unreadable_next_response_does_not_skip_multiple_tracks(self):
        api = self.api()
        api.next.side_effect = ValueError("unreadable response")
        result, client, api = self.run_actions([
            {"tool": "control", "command": "next"}], api=api)
        self.assertFalse(result.succeeded)
        api.next.assert_called_once()
        self.assertEqual(client.chat_json.call_count, 1)

    def test_genre_playlist_selection_uses_description_and_real_context(self):
        api = self.api()
        api.search.return_value = {"playlists": {"items": [None,
            {"name": "Synthwave Nights", "uri": "spotify:playlist:real",
             "description": "<b>Instrumental</b> synthwave &amp; neon nights"}]}}
        client = MagicMock()
        client.chat_json.side_effect = [
            LlmResponse(json.dumps({"tool": "search", "type": "playlist", "query": "synthwave"})),
            LlmResponse(json.dumps({"tool": "select", "index": 0, "shuffle": True}))]
        result = run_music_request("play some synthwave", client=client, api=api)
        self.assertTrue(result.succeeded)
        api.start_playback.assert_called_once_with(context_uri="spotify:playlist:real", device_id="device")
        api.set_shuffle.assert_called_once_with(True, device_id="device")
        observed = json.loads(client.chat_json.call_args_list[1].kwargs["user"])
        row = observed["observations"][0]["results"][0]
        self.assertEqual(row["description"], "Instrumental synthwave & neon nights")
        self.assertEqual(row["type"], "playlist")

    def test_mood_search_can_retry_empty_results_and_preserve_followup_context(self):
        api = self.api()
        api.search.side_effect = [{"playlists": {"items": []}},
            {"playlists": {"items": [{"name": "Heavy Synth", "uri": "spotify:playlist:heavy"}]}}]
        client = MagicMock()
        client.chat_json.side_effect = [LlmResponse(json.dumps(a)) for a in [
            {"tool": "search", "type": "playlist", "query": "heavy instrumental synthwave"},
            {"tool": "search", "type": "playlist", "query": "darksynth instrumental"},
            {"tool": "select", "index": 0, "shuffle": False}]]
        history = [{"request": "play some synthwave"}]
        result = run_music_request("something heavier, no shuffle", context=history, client=client, api=api)
        self.assertTrue(result.succeeded)
        self.assertEqual(api.search.call_count, 2)
        api.start_playback.assert_called_once_with(context_uri="spotify:playlist:heavy", device_id="device")
        api.set_shuffle.assert_called_once_with(False, device_id="device")
        payload = json.loads(client.chat_json.call_args.kwargs["user"])
        self.assertEqual(payload["history"], history)
        self.assertEqual(payload["observations"][0]["results"], [])

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
