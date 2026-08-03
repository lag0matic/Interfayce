"""Constrained LLM interpretation for Spotify requests outside the fast path."""

from __future__ import annotations

from dataclasses import dataclass
from difflib import SequenceMatcher
import json
import re
from typing import Any

from .llm_client import OpenAiCompatibleClient
from .spotify_oauth import SpotifyWebApi


PLAY_TYPES = frozenset({"track", "artist", "artist_top", "album", "playlist"})
CONTROL_COMMANDS = frozenset({
    "pause", "resume", "next", "previous", "restart",
    "volume_up", "volume_down", "volume_set", "mute", "unmute",
})

SYSTEM_PROMPT = """You are the intent router for a private Spotify voice controller.
Return exactly one JSON object and no conversational prose.

Allowed tools:
1. {"tool":"play","type":"track|artist|artist_top|album|playlist","query":"name","artist":"optional artist for track","shuffle":true|false|null}
2. {"tool":"control","command":"pause|resume|next|previous|restart|volume_up|volume_down|volume_set|mute|unmute","value":0-100|null}
3. {"tool":"status"}
4. {"tool":"none"}

Use play/track for a named song and put its performer in artist when known. Use
artist_top for requests for an artist's popular songs. Use none if the request
is not clearly about Spotify. Do not invent tools, URLs, Spotify URIs, or extra
fields. The transcript may contain harmless speech-recognition mistakes."""


class MusicLlmValidationError(ValueError):
    pass


@dataclass(frozen=True)
class MusicLlmIntent:
    tool: str
    play_type: str | None = None
    query: str | None = None
    artist: str | None = None
    shuffle: bool | None = None
    command: str | None = None
    value: int | None = None


@dataclass(frozen=True)
class LlmMusicResult:
    succeeded: bool
    message: str


def _short_text(value: Any, field: str, *, required: bool = False) -> str | None:
    if value is None and not required:
        return None
    if not isinstance(value, str):
        raise MusicLlmValidationError(f"{field} must be text.")
    cleaned = " ".join(value.split())
    if required and not cleaned:
        raise MusicLlmValidationError(f"{field} is required.")
    if len(cleaned) > 160:
        raise MusicLlmValidationError(f"{field} is too long.")
    return cleaned or None


def validate_music_intent(payload: Any) -> MusicLlmIntent:
    if not isinstance(payload, dict) or not isinstance(payload.get("tool"), str):
        raise MusicLlmValidationError("The LLM result must be a tool object.")
    tool = payload["tool"]
    if tool == "play":
        if set(payload) - {"tool", "type", "query", "artist", "shuffle"}:
            raise MusicLlmValidationError("The play result contained unknown fields.")
        play_type = payload.get("type")
        if play_type not in PLAY_TYPES:
            raise MusicLlmValidationError("The play type is not allowed.")
        shuffle = payload.get("shuffle")
        if shuffle is not None and not isinstance(shuffle, bool):
            raise MusicLlmValidationError("shuffle must be true, false, or null.")
        return MusicLlmIntent(
            tool="play", play_type=play_type,
            query=_short_text(payload.get("query"), "query", required=True),
            artist=_short_text(payload.get("artist"), "artist"), shuffle=shuffle,
        )
    if tool == "control":
        if set(payload) - {"tool", "command", "value"}:
            raise MusicLlmValidationError("The control result contained unknown fields.")
        command = payload.get("command")
        if command not in CONTROL_COMMANDS:
            raise MusicLlmValidationError("The control command is not allowed.")
        value = payload.get("value")
        if command == "volume_set":
            if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 100:
                raise MusicLlmValidationError("volume_set requires an integer from 0 through 100.")
        elif value is not None:
            raise MusicLlmValidationError("This control command does not accept a value.")
        return MusicLlmIntent(tool="control", command=command, value=value)
    if tool in {"status", "none"}:
        if set(payload) != {"tool"}:
            raise MusicLlmValidationError(f"{tool} does not accept additional fields.")
        return MusicLlmIntent(tool=tool)
    raise MusicLlmValidationError("The requested tool is not allowed.")


def interpret_music_request(transcript: str,
                            client: OpenAiCompatibleClient | None = None) -> MusicLlmIntent:
    response = (client or OpenAiCompatibleClient()).chat_json(
        system=SYSTEM_PROMPT,
        user="Spoken request: " + json.dumps(transcript, ensure_ascii=False),
    )
    try:
        payload = json.loads(response.content)
    except json.JSONDecodeError as error:
        raise MusicLlmValidationError("The LLM did not return valid intent JSON.") from error
    return validate_music_intent(payload)


def _active_device(api: SpotifyWebApi) -> tuple[str | None, str]:
    devices = [device for device in api.devices() if not device.get("is_restricted")]
    if not devices:
        return None, ""
    device = next((item for item in devices if item.get("is_active")), devices[0])
    return str(device.get("id") or "") or None, str(device.get("name") or "Spotify")


def _items(search: dict[str, Any], play_type: str) -> list[dict[str, Any]]:
    return [item for item in search.get(play_type + "s", {}).get("items", [])
            if isinstance(item, dict)]


def _score_track(track: dict[str, Any], query: str, artist: str | None) -> float:
    name = str(track.get("name", "")).casefold()
    artists = track.get("artists", [])
    track_artist = str(artists[0].get("name", "")).casefold() if artists else ""
    wanted = query.casefold()
    wanted_artist = (artist or "").casefold()
    exact = 10 if name == wanted else 6 if name.startswith(wanted + " ") else 2 if wanted in name else 0
    artist_score = 0
    if wanted_artist:
        artist_score = 8 if track_artist == wanted_artist else (
            4 if wanted_artist in track_artist or track_artist in wanted_artist else -6)
    variants = {"acoustic", "remix", "live", "cover", "remaster", "edit", "instrumental",
                "demo", "karaoke", "reprise", "version", "mix", "dub"}
    penalty = 8 if any(word in name for word in variants) and not any(
        word in wanted for word in variants) else 0
    overlap = len(set(wanted.split()) & set(name.split()))
    return exact + artist_score + overlap - penalty - abs(len(name) - len(wanted)) * 0.05


def _match_text(value: str) -> str:
    return " ".join(re.findall(r"[a-z0-9]+", value.casefold()))


def _similarity(left: str, right: str) -> float:
    return SequenceMatcher(None, _match_text(left), _match_text(right)).ratio()


def execute_music_llm_intent(intent: MusicLlmIntent,
                             api: SpotifyWebApi | None = None) -> LlmMusicResult:
    spotify = api or SpotifyWebApi()
    if intent.tool == "none":
        return LlmMusicResult(False, "I could not map that request to a safe Spotify command.")
    if intent.tool == "status":
        state = spotify.playback_state()
        item = state.get("item") if state else None
        if not item:
            return LlmMusicResult(False, "Spotify is not reporting an active track.")
        artists = ", ".join(str(artist.get("name", "")) for artist in item.get("artists", []))
        return LlmMusicResult(True, f"{item.get('name', 'Unknown track')}, by {artists}.")

    device_id, _device_name = _active_device(spotify)
    if not device_id:
        return LlmMusicResult(False, "No available Spotify device was found. Open Spotify first.")

    if intent.tool == "control":
        command = intent.command
        if command == "pause":
            spotify.pause(device_id=device_id)
            return LlmMusicResult(True, "Playback paused.")
        if command == "resume":
            spotify.start_playback(device_id=device_id)
            return LlmMusicResult(True, "Playback resumed.")
        if command == "next":
            spotify.next(device_id=device_id)
            return LlmMusicResult(True, "Skipped to the next track.")
        if command == "previous":
            spotify.previous(device_id=device_id)
            return LlmMusicResult(True, "Returned to the previous track.")
        if command == "restart":
            spotify.seek(0, device_id=device_id)
            return LlmMusicResult(True, "Restarted the current track.")
        if command == "volume_set":
            spotify.set_volume(intent.value or 0, device_id=device_id)
            return LlmMusicResult(True, f"Spotify volume set to {intent.value}%.")
        if command == "mute":
            spotify.set_volume(0, device_id=device_id)
            return LlmMusicResult(True, "Spotify muted.")
        if command == "unmute":
            spotify.set_volume(50, device_id=device_id)
            return LlmMusicResult(True, "Spotify unmuted to 50%.")
        if command in {"volume_up", "volume_down"}:
            state = spotify.playback_state() or {}
            current = int((state.get("device") or {}).get("volume_percent") or 0)
            updated = max(0, min(100, current + (10 if command == "volume_up" else -10)))
            spotify.set_volume(updated, device_id=device_id)
            return LlmMusicResult(True, f"Spotify volume set to {updated}%.")
        return LlmMusicResult(False, "That Spotify control was not implemented.")

    if intent.tool != "play" or not intent.query or not intent.play_type:
        return LlmMusicResult(False, "The Spotify play request was incomplete.")

    query = intent.query
    if intent.play_type == "track":
        canonical_artist = intent.artist
        if intent.artist:
            artist_choices = _items(
                spotify.search(intent.artist, item_type="artist", limit=5), "artist")
            if not artist_choices:
                return LlmMusicResult(False, f"I could not find artist {intent.artist}.")
            candidate_artist = str(artist_choices[0].get("name", ""))
            if _similarity(intent.artist, candidate_artist) < 0.55:
                return LlmMusicResult(False, f"I could not confidently match artist {intent.artist}.")
            canonical_artist = candidate_artist
        search_query = query + (f" {canonical_artist}" if canonical_artist else "")
        choices = _items(spotify.search(search_query, item_type="track", limit=10), "track")
        if not choices:
            return LlmMusicResult(False, f"I could not find {query} on Spotify.")
        track = max(choices, key=lambda item: _score_track(item, query, canonical_artist))
        artists = track.get("artists", [])
        artist_name = str(artists[0].get("name", "")) if artists else "unknown artist"
        title_similarity = _similarity(query, str(track.get("name", "")))
        artist_similarity = max((_similarity(canonical_artist, str(item.get("name", "")))
                                 for item in artists), default=0.0) if canonical_artist else 1.0
        if title_similarity < 0.72 or artist_similarity < 0.72:
            qualifier = f" by {canonical_artist}" if canonical_artist else ""
            return LlmMusicResult(False, f"I could not confidently match {query}{qualifier}.")
        album_uri = str((track.get("album") or {}).get("uri", "")) or None
        spotify.start_playback(uri=str(track["uri"]), context_uri=album_uri,
                               offset_uri=str(track["uri"]), device_id=device_id)
        return LlmMusicResult(True, f"Playing {track.get('name', query)}, by {artist_name}.")

    if intent.play_type == "playlist" and any(
            phrase in query.casefold() for phrase in ("liked", "saved", "favorite")):
        saved = spotify.request("GET", "/me/tracks", query={"limit": 50})
        uris = [str(item.get("track", {}).get("uri", "")) for item in saved.get("items", [])]
        uris = [uri for uri in uris if uri]
        if not uris:
            return LlmMusicResult(False, "Your Liked Songs list appears to be empty.")
        spotify.start_playback(uris=uris, device_id=device_id)
        spotify.set_shuffle(intent.shuffle is not False, device_id=device_id)
        return LlmMusicResult(True, "Playing your Liked Songs.")

    search_type = "artist" if intent.play_type == "artist_top" else intent.play_type
    choices = _items(spotify.search(query, item_type=search_type, limit=5), search_type)
    if not choices:
        return LlmMusicResult(False, f"I could not find {query} on Spotify.")
    match = choices[0]
    name = str(match.get("name", query))
    if intent.play_type == "artist_top":
        # Spotify removed /artists/{id}/top-tracks from Development Mode in 2026.
        # Search order is Spotify's best available relevance ordering; retain only
        # tracks whose credited artist matches the selected artist.
        tracks = _items(spotify.search(name, item_type="track", limit=10), "track")
        wanted = name.casefold()
        matching = [track for track in tracks if any(
            str(artist.get("name", "")).casefold() == wanted
            for artist in track.get("artists", []))]
        uris = [str(track.get("uri", "")) for track in matching if track.get("uri")]
        if not uris:
            return LlmMusicResult(False, f"Spotify returned no matching tracks for {name}.")
        spotify.start_playback(uris=uris, device_id=device_id)
        return LlmMusicResult(True, f"Playing songs by {name}.")
    spotify.start_playback(context_uri=str(match["uri"]), device_id=device_id)
    shuffle = intent.shuffle if intent.shuffle is not None else intent.play_type in {"artist", "playlist"}
    spotify.set_shuffle(shuffle, device_id=device_id)
    label = "artist" if intent.play_type == "artist" else intent.play_type
    return LlmMusicResult(True, f"Playing {label} {name}.")
