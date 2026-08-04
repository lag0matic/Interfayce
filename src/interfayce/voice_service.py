"""Localhost-only resident service for bounded microphone/STT work."""

from __future__ import annotations

import asyncio
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import threading

from .comms import CommsDictation
from .battery_alerts import BatteryAlertMonitor
from .kokoro import speak_in_background
from .local_service import get_or_create_token, request_is_authorized
from .llm_client import LlmError, OpenAiCompatibleClient
from .music_llm import MusicLlmValidationError, execute_music_llm_intent, interpret_music_request
from .parakeet_stt import ParakeetTranscriber, capture_microphone_once
from .osc import VrchatOscClient
from .settings import (adjust_broadcast_gain, adjust_tts_volume, comms_shortcut_labels,
                       desktop_favorites_wire_text, load_settings, settings_wire_text,
                       toggle_tts_mute)
from .song_announcer import ResidentSongAnnouncer
from .spotify_oauth import SpotifyOAuthError
from .voice import MusicCommandResult, MusicIntentKind, execute_music_intent, parse_music_intent
from .windows_media import WindowsSpotifyMedia


DEFAULT_PORT = 43817
LOGGER = logging.getLogger("interfayce.voice")


def voice_log_path() -> Path:
    configured = os.environ.get("INTERFAYCE_VOICE_LOG")
    if configured:
        return Path(configured).expanduser()
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return local / "Interfayce" / "logs" / "voice-service.log"


def configure_logging() -> Path:
    path = voice_log_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(path, maxBytes=1_000_000, backupCount=2, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    LOGGER.handlers.clear()
    LOGGER.addHandler(handler)
    LOGGER.setLevel(logging.INFO)
    LOGGER.propagate = False
    return path


def _safe_field(value: str) -> str:
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


class VoiceRuntime:
    def __init__(self) -> None:
        self.transcriber = ParakeetTranscriber()
        self.command_lock = threading.Lock()
        self.comms = CommsDictation(self.transcriber, self.command_lock)
        self.battery_alerts = BatteryAlertMonitor()
        self._song_media = WindowsSpotifyMedia()
        self._song_read_failure_logged = False
        osc = VrchatOscClient()
        self.song_announcer = ResidentSongAnnouncer(
            self._read_current_song,
            self._announce_song,
            osc.clear_chatbox,
        )
        LOGGER.info("Parakeet model directory: %s; feature_dim=%s; threads=%s",
            self.transcriber.files.directory, self.transcriber.feature_dim,
            self.transcriber.threads)

    @staticmethod
    def _announce_song(message: str) -> None:
        VrchatOscClient().send_chatbox_message(message)
        LOGGER.info("Sent Spotify track announcement: chars=%s", len(message))

    def _read_current_song(self):
        try:
            track = asyncio.run(self._song_media.current_track())
            self._song_read_failure_logged = False
            return track
        except Exception:
            if not self._song_read_failure_logged:
                LOGGER.exception("Spotify song announcement query failed")
                self._song_read_failure_logged = True
            return None

    def warm(self) -> None:
        try:
            LOGGER.info("Warming Parakeet model")
            self.transcriber.warm()
            LOGGER.info("Parakeet model ready")
        except Exception:
            LOGGER.exception("Parakeet warm-up failed")

    def music_command(self) -> str:
        if not self.command_lock.acquire(blocking=False):
            return "BUSY\t\tVoice capture is already active."
        try:
            try:
                LOGGER.info("Music microphone capture started")
                audio = capture_microphone_once()
                LOGGER.info("Music microphone capture completed: rate=%s width=%s bytes=%s",
                    getattr(audio, "sample_rate", "?"), getattr(audio, "sample_width", "?"),
                    len(getattr(audio, "frame_data", b"")))
            except Exception as error:
                LOGGER.exception("Microphone capture failed")
                return f"ERROR\t\t{_safe_field(str(error))}"
            try:
                transcript = self.transcriber.transcribe(audio)
            except Exception as error:
                LOGGER.exception("Parakeet transcription failed")
                return f"ERROR\t\t{_safe_field(str(error))}"
            intent = parse_music_intent(transcript)
            LOGGER.info("Music transcript classified: chars=%s intent=%s",
                        len(transcript), intent.kind.value)
            if intent.kind is MusicIntentKind.UNKNOWN and OpenAiCompatibleClient().configured:
                try:
                    llm_intent = interpret_music_request(transcript)
                    LOGGER.info("Validated LLM music tool=%s play_type=%s query_chars=%s "
                                "artist_chars=%s command=%s", llm_intent.tool,
                                llm_intent.play_type, len(llm_intent.query or ""),
                                len(llm_intent.artist or ""), llm_intent.command)
                    result = execute_music_llm_intent(llm_intent)
                except (LlmError, MusicLlmValidationError) as error:
                    LOGGER.warning("LLM music fallback rejected: %s", error)
                    result = MusicCommandResult(False, "I could not safely interpret that request.")
                except SpotifyOAuthError as error:
                    LOGGER.warning("Spotify OAuth action failed: %s", error)
                    result = MusicCommandResult(False, "Spotify rejected that command.")
            else:
                result = asyncio.run(execute_music_intent(intent))
            LOGGER.info("Music command completed: succeeded=%s response_chars=%s",
                        result.succeeded, len(result.message))
            # In-headset failures are as important as successes; the user should
            # not need to stop and read the wrist to learn that nothing happened.
            speak_in_background(result.message)
            state = "OK" if result.succeeded else "NO_MATCH"
            return f"{state}\t{_safe_field(transcript)}\t{_safe_field(result.message)}"
        finally:
            self.command_lock.release()

    def current_music(self) -> str:
        try:
            track, playing = asyncio.run(WindowsSpotifyMedia().current_track_and_playback())
            return "" if track is None else (
                f"{'PLAYING' if playing else 'PAUSED'}\t"
                f"{_safe_field(track.artist)}\t{_safe_field(track.title)}")
        except Exception:
            LOGGER.exception("Music status query failed")
            return ""

    def music_control(self, operation: str) -> bool:
        media = WindowsSpotifyMedia()
        actions = {
            "previous": media.previous_track,
            "toggle": media.toggle_play_pause,
            "next": media.next_track,
        }
        action = actions.get(operation)
        if action is None:
            return False
        try:
            return bool(asyncio.run(action()))
        except Exception:
            LOGGER.exception("Music control failed: %s", operation)
            return False

    def music_art(self) -> bytes:
        try:
            return asyncio.run(WindowsSpotifyMedia().current_art_bytes()) or b""
        except Exception:
            LOGGER.exception("Music artwork query failed")
            return b""

    def comms_status(self) -> str:
        return self.comms.snapshot().wire_text()

    def toggle_comms(self) -> str:
        self.comms.set_silence_auto_stop_seconds(
            load_settings().comms_silence_timeout_seconds)
        return self.comms.toggle().wire_text()

    def clear_comms(self) -> str:
        try:
            return self.comms.clear().wire_text()
        except Exception as error:
            LOGGER.exception("Comms chatbox clear failed")
            return f"ERROR\t{_safe_field(str(error))}"

    def send_comms_shortcut(self, index: int) -> str:
        try:
            shortcuts = load_settings().comms_shortcuts
            if index < 0 or index >= len(shortcuts):
                raise ValueError("Comms shortcut does not exist.")
            label, message = shortcuts[index]
            if not label or not message:
                raise ValueError("Comms shortcut is not configured.")
            return self.comms.send_shortcut(message).wire_text()
        except Exception as error:
            LOGGER.exception("Comms shortcut failed: %s", index)
            return f"ERROR\t{_safe_field(str(error))}"


def serve_voice(*, port: int = DEFAULT_PORT, warm: bool = False) -> None:
    log_path = configure_logging()
    LOGGER.info("Voice service starting on 127.0.0.1:%s; log=%s", port, log_path)
    runtime = VoiceRuntime()
    auth_token = get_or_create_token()

    class Handler(BaseHTTPRequestHandler):
        def _authorized(self) -> bool:
            if request_is_authorized(self.headers, port=port, token=auth_token):
                return True
            self._reply(403, "forbidden")
            return False

        def _reply_bytes(self, status: int, encoded: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def _reply(self, status: int, body: str) -> None:
            self._reply_bytes(status, body.encode("utf-8"), "text/plain; charset=utf-8")

        def do_GET(self) -> None:  # noqa: N802
            if not self._authorized():
                return
            if self.path == "/health":
                self._reply(200, "ready")
            elif self.path == "/music/current":
                self._reply(200, runtime.current_music())
            elif self.path == "/music/art":
                self._reply_bytes(200, runtime.music_art(), "application/octet-stream")
            elif self.path == "/settings":
                self._reply(200, settings_wire_text())
            elif self.path == "/comms/status":
                self._reply(200, runtime.comms_status())
            elif self.path == "/comms/shortcuts":
                self._reply(200, comms_shortcut_labels())
            elif self.path == "/desktop/favorites":
                self._reply(200, desktop_favorites_wire_text())
            else:
                self._reply(404, "not found")

        def do_POST(self) -> None:  # noqa: N802
            if not self._authorized():
                return
            if self.path == "/listen/music":
                self._reply(200, runtime.music_command())
            elif self.path.startswith("/music/control/"):
                operation = self.path.rsplit("/", 1)[-1]
                self._reply(200, "ok" if runtime.music_control(operation) else "unavailable")
            elif self.path == "/comms/toggle":
                self._reply(200, runtime.toggle_comms())
            elif self.path == "/comms/clear":
                self._reply(200, runtime.clear_comms())
            elif self.path.startswith("/comms/shortcut/"):
                try:
                    index = int(self.path.rsplit("/", 1)[-1])
                except ValueError:
                    self._reply(400, "ERROR\tInvalid shortcut index.")
                else:
                    self._reply(200, runtime.send_comms_shortcut(index))
            elif self.path == "/tts/announce":
                try:
                    length = max(0, min(int(self.headers.get("Content-Length", "0")), 512))
                except ValueError:
                    length = 0
                message = self.rfile.read(length).decode("utf-8", errors="replace").strip()
                if not message:
                    self._reply(400, "empty announcement")
                else:
                    speak_in_background(message)
                    self._reply(200, "queued")
            elif self.path == "/battery/status":
                try:
                    length = max(0, min(int(self.headers.get("Content-Length", "0")), 2048))
                except ValueError:
                    length = 0
                readings: dict[str, int] = {}
                for line in self.rfile.read(length).decode("utf-8", errors="replace").splitlines():
                    name, separator, value = line.partition("=")
                    if separator and name.strip():
                        try:
                            readings[name.strip()] = int(value.strip())
                        except ValueError:
                            continue
                announcement = runtime.battery_alerts.observe(readings)
                if announcement:
                    speak_in_background(announcement)
                self._reply(200, announcement or "quiet")
            elif self.path == "/settings/tts/volume/up":
                self._reply(200, settings_wire_text(adjust_tts_volume(0.1)))
            elif self.path == "/settings/tts/volume/down":
                self._reply(200, settings_wire_text(adjust_tts_volume(-0.1)))
            elif self.path == "/settings/tts/mute/toggle":
                self._reply(200, settings_wire_text(toggle_tts_mute()))
            elif self.path == "/settings/broadcast/gain/up":
                self._reply(200, settings_wire_text(adjust_broadcast_gain(3.0)))
            elif self.path == "/settings/broadcast/gain/down":
                self._reply(200, settings_wire_text(adjust_broadcast_gain(-3.0)))
            elif self.path == "/shutdown":
                self._reply(200, "stopping")
                threading.Thread(target=self.server.shutdown, daemon=True).start()
            else:
                self._reply(404, "not found")

        def log_message(self, _format: str, *args: object) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    runtime.song_announcer.start()
    LOGGER.info("Spotify OSC song announcer started")
    if warm:
        threading.Thread(target=runtime.warm, name="InterfayceParakeetWarm", daemon=True).start()
    try:
        server.serve_forever()
    finally:
        runtime.song_announcer.stop()
        LOGGER.info("Voice service stopping")
        server.server_close()


def configured_port() -> int:
    return int(os.environ.get("INTERFAYCE_VOICE_PORT", str(DEFAULT_PORT)))
