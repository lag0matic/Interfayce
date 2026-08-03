"""Small explicit commands for bringing up Interfayce safely."""

from __future__ import annotations

import argparse
import asyncio
import time
from contextlib import suppress

from .osc import VrchatOscClient
from .song_announcer import StableSongChangeWatcher
from .steamvr import (
    read_index_b_buttons,
    read_index_b_buttons_from_system,
    read_index_controller_poses,
    read_index_controller_status,
    read_standing_origin,
)
from .steamvr_input import SteamVrInput
from .windows_media import WindowsSpotifyMedia


async def _clear_chatbox_after(osc: VrchatOscClient, delay_seconds: float = 7.0) -> None:
    await asyncio.sleep(delay_seconds)
    osc.clear_chatbox()
    print("Sent experimental chatbox clear pulse.", flush=True)


async def _watch_spotify(*, host: str, port: int, poll_seconds: float) -> None:
    spotify = WindowsSpotifyMedia()
    osc = VrchatOscClient(host=host, port=port)
    watcher = StableSongChangeWatcher()
    has_seen_track = False
    print(
        "Watching Spotify. Existing track will not be announced; press Ctrl+C to stop.",
        flush=True,
    )
    while True:
        track = await spotify.current_track()
        if track is not None and not has_seen_track:
            has_seen_track = True
            print("Spotify session detected and synced.", flush=True)
        announcement = watcher.observe(track)
        if announcement:
            osc.send_chatbox_message(announcement.chatbox_text())
            print("Sent Spotify track announcement.", flush=True)
            asyncio.create_task(_clear_chatbox_after(osc))
        await asyncio.sleep(poll_seconds)


def main() -> None:
    parser = argparse.ArgumentParser(description="Interfayce utilities")
    subcommands = parser.add_subparsers(dest="command", required=True)

    chatbox_test = subcommands.add_parser(
        "chatbox-test", help="Send one harmless OSC test message to VRChat."
    )
    chatbox_test.add_argument("--host", default="127.0.0.1")
    chatbox_test.add_argument("--port", default=9000, type=int)

    spotify_watch = subcommands.add_parser(
        "spotify-watch", help="Announce stable Spotify track changes in VRChat."
    )
    spotify_watch.add_argument("--host", default="127.0.0.1")
    spotify_watch.add_argument("--port", default=9000, type=int)
    spotify_watch.add_argument("--poll-seconds", default=1.0, type=float)

    media_control = subcommands.add_parser(
        "spotify-control", help="Send one Spotify transport command through Windows media controls."
    )
    media_control.add_argument("operation", choices=("previous", "toggle", "next"))
    subcommands.add_parser("spotify-current", help="Print the current Spotify artist and title as one tab-separated line.")
    spotify_art = subcommands.add_parser("spotify-art", help="Save the current Spotify thumbnail to a local PNG/JPEG file.")
    spotify_art.add_argument("--output", required=True)
    voice_service = subcommands.add_parser(
        "voice-service", help="Run the localhost-only Parakeet voice service."
    )
    voice_service.add_argument("--port", default=43817, type=int)
    voice_service.add_argument("--warm", action="store_true")
    voice_intent = subcommands.add_parser(
        "voice-intent", help="Classify one deterministic Music transcript without acting on it."
    )
    voice_intent.add_argument("transcript")
    subcommands.add_parser(
        "voice-model-status", help="Locate the configured Parakeet model without loading it."
    )
    spotify_oauth = subcommands.add_parser(
        "spotify-oauth-connect", help="Connect Spotify through browser-based OAuth PKCE."
    )
    spotify_oauth.add_argument("--client-id", default="")
    subcommands.add_parser(
        "spotify-oauth-status", help="Validate the protected Spotify OAuth session."
    )
    subcommands.add_parser(
        "spotify-oauth-disconnect", help="Delete the protected Spotify OAuth token."
    )
    spotify_search = subcommands.add_parser(
        "spotify-search", help="Run an authenticated Spotify search diagnostic."
    )
    spotify_search.add_argument("query")
    spotify_search.add_argument("--type", choices=("track", "album", "artist", "playlist"), default="track")
    subcommands.add_parser(
        "llm-key-dialog", help="Open a local protected-input dialog for the LLM API token."
    )
    subcommands.add_parser(
        "llm-status", help="Report the configured LLM profile without exposing its token."
    )

    steamvr_status = subcommands.add_parser(
        "steamvr-status", help="Show Index controller battery state from SteamVR."
    )
    steamvr_poses = subcommands.add_parser(
        "steamvr-poses", help="Show read-only Index controller positions from SteamVR."
    )
    steamvr_baseline = subcommands.add_parser(
        "steamvr-baseline", help="Read the current standing-origin baseline without changing it."
    )
    subcommands.add_parser(
        "steamvr-input-watch",
        help="Register Interfayce's Index binding and report drag-action presses; no movement.",
    )
    subcommands.add_parser(
        "steamvr-button-watch",
        help="Report physical Index B presses directly; no movement.",
    )

    arguments = parser.parse_args()
    if arguments.command == "chatbox-test":
        message = "♫ Test — Interfayce"
        VrchatOscClient(host=arguments.host, port=arguments.port).send_chatbox_message(message)
        print("Sent VRChat chatbox test message.")
    elif arguments.command == "spotify-watch":
        with suppress(KeyboardInterrupt):
            asyncio.run(
                _watch_spotify(
                    host=arguments.host,
                    port=arguments.port,
                    poll_seconds=arguments.poll_seconds,
                )
            )
    elif arguments.command == "spotify-control":
        spotify = WindowsSpotifyMedia()
        operation = {
            "previous": spotify.previous_track,
            "toggle": spotify.toggle_play_pause,
            "next": spotify.next_track,
        }[arguments.operation]
        if not asyncio.run(operation()):
            print("Spotify media session was not available.")
    elif arguments.command == "spotify-current":
        track = asyncio.run(WindowsSpotifyMedia().current_track())
        if track is not None:
            print(f"{track.artist}\t{track.title}")
    elif arguments.command == "spotify-art":
        art = asyncio.run(WindowsSpotifyMedia().current_art_bytes())
        if art:
            from pathlib import Path

            Path(arguments.output).write_bytes(art)
    elif arguments.command == "voice-service":
        from .voice_service import serve_voice

        serve_voice(port=arguments.port, warm=arguments.warm)
    elif arguments.command == "voice-intent":
        from .voice import parse_music_intent

        print(parse_music_intent(arguments.transcript).kind.value)
    elif arguments.command == "voice-model-status":
        from .parakeet_stt import discover_parakeet_model

        print(discover_parakeet_model().directory)
    elif arguments.command == "spotify-oauth-connect":
        from .settings import load_settings
        from .spotify_oauth import SpotifyWebApi, connect

        client_id = arguments.client_id or load_settings().spotify_client_id
        connect(client_id)
        profile = SpotifyWebApi(client_id).profile()
        print(f"Connected Spotify as {profile.display_name} ({profile.product}).")
    elif arguments.command == "spotify-oauth-status":
        from .spotify_oauth import SpotifyOAuthError, SpotifyWebApi

        try:
            profile = SpotifyWebApi().profile()
            print(f"CONNECTED\t{profile.display_name}\t{profile.product}")
        except SpotifyOAuthError as error:
            print(f"DISCONNECTED\t{error}")
    elif arguments.command == "spotify-oauth-disconnect":
        from .spotify_oauth import disconnect

        print("Spotify disconnected." if disconnect() else "Spotify was already disconnected.")
    elif arguments.command == "spotify-search":
        from .spotify_oauth import SpotifyWebApi

        data = SpotifyWebApi().search(arguments.query, item_type=arguments.type)
        collection = data.get(arguments.type + "s", {}).get("items", [])
        for item in collection:
            artists = ", ".join(artist.get("name", "") for artist in item.get("artists", []))
            print(f"{item.get('uri', '')}\t{item.get('name', '')}\t{artists}")
    elif arguments.command == "llm-key-dialog":
        from .llm_client import show_api_key_dialog

        show_api_key_dialog()
    elif arguments.command == "llm-status":
        from .llm_client import OpenAiCompatibleClient

        client = OpenAiCompatibleClient()
        print(f"{'CONFIGURED' if client.configured else 'MISSING_KEY'}\t"
              f"{client.settings.llm_endpoint}\t{client.settings.llm_model}")
    elif arguments.command == "steamvr-status":
        status = read_index_controller_status()
        if not status.available:
            print("SteamVR is unavailable: " + (status.detail or "unknown error"))
            return
        for hand in ("left_controller", "right_controller"):
            reading = status.readings[hand]
            if not reading.connected:
                print(hand + ": disconnected")
            else:
                suffix = " (charging)" if reading.charging else ""
                print(hand + f": {reading.percent}%" + suffix)
    elif arguments.command == "steamvr-poses":
        status = read_index_controller_poses()
        if not status.available:
            print("SteamVR poses are unavailable: " + (status.detail or "unknown error"))
            return
        for hand in ("left_controller", "right_controller"):
            pose = status.poses[hand]
            if not pose.connected:
                print(hand + ": disconnected")
            elif not pose.tracked:
                print(hand + ": connected, not currently tracked")
            else:
                x, y, z = pose.position or (0.0, 0.0, 0.0)
                print(hand + f": x={x:.3f} m, y={y:.3f} m, z={z:.3f} m")
    elif arguments.command == "steamvr-baseline":
        origin = read_standing_origin()
        if not origin.available or origin.matrix is None:
            print("SteamVR standing-origin baseline unavailable: " + (origin.detail or "unknown error"))
            return
        print("SteamVR standing-origin baseline:")
        for row in origin.matrix:
            print(" ".join(f"{value:.6f}" for value in row))
    elif arguments.command == "steamvr-input-watch":
        input_reader = SteamVrInput()
        print("Interfayce binding registered. Watching B double-tap drag actions; Ctrl+C to stop.", flush=True)
        previous = (False, False)
        try:
            while True:
                state = input_reader.read_drag_actions()
                current = (state.left_active, state.right_active)
                if current != previous or state.left_changed or state.right_changed:
                    print(
                        f"left_drag={current[0]} changed={state.left_changed} "
                        f"right_drag={current[1]} changed={state.right_changed}",
                        flush=True,
                    )
                    previous = current
                time.sleep(0.01)
        except KeyboardInterrupt:
            pass
        finally:
            input_reader.close()
    elif arguments.command == "steamvr-button-watch":
        print("Watching physical Index B buttons; Ctrl+C to stop.", flush=True)
        previous = (False, False)
        import openvr

        openvr.init(openvr.VRApplication_Background)
        try:
            system = openvr.VRSystem()
            while True:
                status = read_index_b_buttons_from_system(openvr, system)
                current = (
                    status.pressed["left_controller"],
                    status.pressed["right_controller"],
                )
                if current != previous:
                    print(f"left_b={current[0]} right_b={current[1]}", flush=True)
                    previous = current
                time.sleep(0.01)
        except KeyboardInterrupt:
            pass
        finally:
            openvr.shutdown()


if __name__ == "__main__":
    main()
