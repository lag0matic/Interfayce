"""Localhost-only resident service for bounded microphone/STT work."""

from __future__ import annotations

import asyncio
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import threading

from .service_health import ServiceHealth
from .configured_stt import ConfiguredTranscriber
from .comms import CommsDictation
from .chatbox import ChatboxCoordinator
from .capture_cue import play_capture_cue, play_result_cue, capture_with_cues
from .assistant import AssistantSnapshot, AssistantState
from .assistant_harness import AssistantHarness, tts_text
from .assistant_session import AssistantSession
from .battery_alerts import BatteryAlertMonitor
from .kokoro import speak_in_background, synthesize
from .local_service import get_or_create_token, request_is_authorized
from .llm_client import LlmError, OpenAiCompatibleClient
from .music_conversation import run_music_request
from .music_llm import MusicConversationMemory, MusicLlmValidationError
from .parakeet_stt import ParakeetTranscriber, capture_microphone_once
from .remote_stt import RemoteSttTranscriber
from .osc import VrchatOscClient
from .settings import (adjust_broadcast_gain, adjust_tts_volume, comms_shortcut_labels,
                       desktop_favorites_wire_text, load_settings, record_desktop_recent,
                       settings_wire_text, toggle_song_announce, toggle_tts_mute)
from .song_announcer import ResidentSongAnnouncer
from .spotify_oauth import SpotifyOAuthError
from .voice import MusicCommandResult, MusicIntentKind, execute_music_intent, parse_music_intent
from .windows_media import WindowsSpotifyMedia


DEFAULT_PORT = 43817
LOGGER = logging.getLogger("interfayce.voice")


class BoundedVoiceServer(ThreadingHTTPServer):
    """Bound waiting clients without blocking the accept loop or voice commands."""

    connection_timeout = 5.0
    max_connections = 16

    def __init__(self, *args, **kwargs):
        self._connection_slots = threading.BoundedSemaphore(self.max_connections)
        super().__init__(*args, **kwargs)

    def get_request(self):
        request, address = super().get_request()
        request.settimeout(self.connection_timeout)
        return request, address

    def process_request(self, request, client_address):
        if not self._connection_slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._connection_slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._connection_slots.release()


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


def capture_microphone_with_cues():
    """Bracket capture with local feedback without discarding opening audio."""

    return capture_with_cues(
        capture_microphone_once, ambient_seconds=0.0, cue=play_capture_cue)


class VoiceRuntime:
    def __init__(self) -> None:
        local_transcriber = ParakeetTranscriber()
        settings = load_settings()
        self.transcriber = ConfiguredTranscriber(local_transcriber)
        self.health = ServiceHealth(self.transcriber)
        self.command_lock = threading.Lock()
        self._warm_lock = threading.Lock()
        self._assistant_lock = threading.Lock()
        self._assistant_status = "READY"
        self._assistant_transcript = ""
        self._assistant_response = ""
        self.assistant = AssistantHarness(on_state=self._on_assistant_state)
        self.assistant_session = AssistantSession(self.assistant)
        self.health.assistant_health = lambda: (self.assistant_session.codex.health()
            if self.assistant_session.backend == "codex" else None)
        self._assistant_speech_cancel = threading.Event()
        self._answer_capture_lock = threading.Lock()
        self.music_conversation = MusicConversationMemory()
        self.chatbox = ChatboxCoordinator(
            song_enabled=lambda: load_settings().song_announce_enabled)
        self.comms = CommsDictation(
            self.transcriber, self.command_lock, cue=play_capture_cue,
            result_cue=play_result_cue, osc=self.chatbox)
        self.battery_alerts = BatteryAlertMonitor()
        self._song_media = WindowsSpotifyMedia()
        self._song_read_failure_logged = False
        self.song_announcer = ResidentSongAnnouncer(
            self._read_current_song,
            self._announce_song,
            self.chatbox.clear_chatbox,
            clear_seconds=None,
        )
        LOGGER.info("STT configured: %s", self.transcriber.description)

    def _set_assistant_status(self, status: str, *, transcript: str | None = None,
                              response: str | None = None) -> None:
        with self._assistant_lock:
            self._assistant_status = status
            if transcript is not None:
                self._assistant_transcript = transcript
            if response is not None:
                self._assistant_response = response

    def _on_assistant_state(self, snapshot: AssistantSnapshot) -> None:
        if snapshot.state is AssistantState.USING_TOOL:
            status = ("SEARCHING" if snapshot.tool_name == "search_web"
                      else "READING" if snapshot.tool_name == "open_search_result"
                      else "CHECKING")
        else:
            status = {
                AssistantState.THINKING: "THINKING",
                AssistantState.RESPONDING: "RESPONDING",
                AssistantState.CANCELLED: "CANCELLED",
                AssistantState.ERROR: "ERROR",
            }.get(snapshot.state, "READY")
        self._set_assistant_status(status,
                                   response=snapshot.response or None)

    def assistant_status(self) -> str:
        with self._assistant_lock:
            return "\t".join((
                _safe_field(self._assistant_status),
                _safe_field(self._assistant_transcript),
                _safe_field(self._assistant_response),
            ))

    def assistant_snapshot(self) -> str:
        with self._assistant_lock:
            snapshot = {"version": 1, "backend": self.assistant_session.backend,
                        "status": self._assistant_status, "transcript": self._assistant_transcript,
                        "response": self._assistant_response, "active": self.command_lock.locked(),
                        "pending": None}
        if self.assistant_session.backend == "codex":
            snapshot["pending"] = self.assistant_session.codex.snapshot()["pending"]
        return json.dumps(snapshot)

    def select_assistant(self) -> str:
        if not self.command_lock.acquire(blocking=False):
            return self.assistant_snapshot()
        try:
            self._assistant_speech_cancel.set()
            self.assistant_session.select()
            self._set_assistant_status("READY", transcript="", response="")
            return self.assistant_snapshot()
        finally:
            self.command_lock.release()

    def dictate_assistant_answer(self, token):
        if not self._answer_capture_lock.acquire(blocking=False):
            return
        try:
            card = self.assistant_session.codex.snapshot()['pending']
            if not card or card['token'] != token or not card.get('canDictate'):
                return
            audio = capture_microphone_with_cues()
            answer = self.transcriber.transcribe(audio).strip()
            if answer:
                self.assistant_session.codex.dictate_answer(token, answer)
        except Exception:
            LOGGER.exception('Assistant answer capture failed')
        finally:
            self._answer_capture_lock.release()

    def _codex_update(self):
        snapshot = self.assistant_session.codex.snapshot()
        self._set_assistant_status(snapshot["status"], response=snapshot["response"])

    def restore_assistant(self):
        if self.assistant_session.backend != 'codex' or not self.command_lock.acquire(blocking=False):
            return
        try:
            self.assistant_session.codex.restore()
            self._codex_update()
        except Exception as error:
            self._set_assistant_status('ERROR', response=str(error))
        finally:
            self.command_lock.release()

    def assistant_command(self) -> str:
        if not self.command_lock.acquire(blocking=False):
            self._set_assistant_status("BUSY", response="Voice capture is already active.")
            return self.assistant_status()
        try:
            session = self.assistant_session
            generation = session.generation
            self._assistant_speech_cancel.set()
            speech_cancel = threading.Event()
            self._assistant_speech_cancel = speech_cancel
            self._set_assistant_status("LISTENING", transcript="", response="")
            try:
                LOGGER.info("Assistant microphone capture started")
                audio = capture_microphone_with_cues()
                transcript = self.transcriber.transcribe(audio)
            except Exception as error:
                LOGGER.exception("Assistant capture or transcription failed")
                self._set_assistant_status("ERROR", response=_safe_field(str(error)))
                return self.assistant_status()
            transcript = _safe_field(transcript)
            if session.generation != generation:
                self._set_assistant_status("CANCELLED")
                return self.assistant_status()
            self._set_assistant_status("THINKING", transcript=transcript, response="")
            if session.backend == "codex":
                try:
                    answer = session.codex.ask(transcript, self._codex_update, cancel=speech_cancel)
                    if answer and not speech_cancel.is_set():
                        speak_in_background(tts_text(answer), cancel=speech_cancel)
                except Exception as error:
                    previous = session.codex.snapshot()['response']
                    self._set_assistant_status("ERROR", response=previous + '\n\n' + str(error))
                return self.assistant_status()
            result = self.assistant.ask(transcript)
            spoken = tts_text(result.answer)
            if spoken and not speech_cancel.is_set():
                speak_in_background(spoken, cancel=speech_cancel)
            self._set_assistant_status(
                "ANSWER" if result.succeeded else "ERROR",
                response=result.answer,
            )
            LOGGER.info("Assistant command completed: succeeded=%s transcript_chars=%s "
                        "response_chars=%s tools=%s", result.succeeded, len(transcript),
                        len(result.answer), ",".join(result.tools_used) or "none")
            return self.assistant_status()
        finally:
            # A cancelled turn may still have a bounded dictation capture winding
            # down. Keep the shared microphone slot until that capture finishes.
            with self._answer_capture_lock:
                self.command_lock.release()

    def clear_assistant(self) -> str:
        if not self.command_lock.acquire(blocking=False):
            return self.assistant_status()
        try:
            self._assistant_speech_cancel.set()
            self.assistant_session.clear()
            self._set_assistant_status("READY", transcript="", response="")
            return self.assistant_status()
        finally:
            self.command_lock.release()

    def cancel_assistant(self) -> str:
        self._set_assistant_status("STOPPING")
        self._assistant_speech_cancel.set()
        self.assistant_session.cancel()
        if not self.command_lock.locked():
            self._set_assistant_status("CANCELLED")
        return self.assistant_status()

    def _announce_song(self, message: str) -> None:
        if not load_settings().song_announce_enabled:
            LOGGER.info("Spotify track announcement suppressed by wrist setting")
            return
        self.chatbox.announce_song(message)
        LOGGER.info("Queued Spotify track announcement: chars=%s", len(message))

    def _read_current_song(self):
        try:
            track = asyncio.run(self._song_media.current_track())
            self._song_read_failure_logged = False
            self.health.set("SPOTIFY", "good" if track is not None else "offline",
                            "Media session available" if track is not None else "No Spotify media session")
            return track
        except Exception:
            self.health.set("SPOTIFY", "offline", "Media session unavailable")
            if not self._song_read_failure_logged:
                LOGGER.exception("Spotify song announcement query failed")
                self._song_read_failure_logged = True
            return None

    def warm(self) -> None:
        if not self._warm_lock.acquire(blocking=False):
            LOGGER.info("Speech model warm-up is already running")
            return
        try:
            try:
                LOGGER.info("Warming configured STT")
                self.transcriber.warm()
                LOGGER.info("Configured STT ready")
            except Exception:
                LOGGER.exception("Configured STT warm-up failed")
            try:
                LOGGER.info("Warming configured TTS")
                synthesize("Ready.")
                LOGGER.info("Configured TTS ready")
            except Exception:
                LOGGER.exception("Configured TTS warm-up failed")
        finally:
            self._warm_lock.release()

    def music_command(self) -> str:
        if not self.command_lock.acquire(blocking=False):
            return "BUSY\t\tVoice capture is already active."
        try:
            try:
                LOGGER.info("Music microphone capture started")
                audio = capture_microphone_with_cues()
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
            action = f"deterministic:{intent.kind.value}"
            if OpenAiCompatibleClient().configured:
                try:
                    action = "llm:conversation"
                    result = run_music_request(
                        transcript, context=self.music_conversation.recent())
                except (LlmError, MusicLlmValidationError) as error:
                    LOGGER.warning("LLM music fallback rejected: %s", error)
                    result = MusicCommandResult(False, "I couldn't understand that request. Could you phrase it another way?")
                except SpotifyOAuthError as error:
                    LOGGER.warning("Spotify OAuth action failed: %s", error)
                    result = MusicCommandResult(False, "Spotify rejected that command.")
            else:
                result = asyncio.run(execute_music_intent(intent))
            LOGGER.info("Music command completed: succeeded=%s response_chars=%s",
                        result.succeeded, len(result.message))
            self.music_conversation.remember(
                transcript=transcript, action=action,
                succeeded=result.succeeded, response=result.message)
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
            accepted = bool(asyncio.run(action()))
            LOGGER.info("Wrist music transport: operation=%s accepted=%s", operation, accepted)
            return accepted
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
        return self.comms.toggle().wire_text()

    def start_comms(self) -> str:
        return self.comms.start().wire_text()

    def stop_comms(self) -> str:
        return self.comms.stop().wire_text()

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
            elif self.path == "/services/status":
                self._reply(200, runtime.health.wire())
            elif self.path == "/music/current":
                self._reply(200, runtime.current_music())
            elif self.path == "/music/art":
                self._reply_bytes(200, runtime.music_art(), "application/octet-stream")
            elif self.path == "/settings":
                self._reply(200, settings_wire_text())
            elif self.path == "/comms/status":
                self._reply(200, runtime.comms_status())
            elif self.path == "/assistant/status":
                self._reply(200, runtime.assistant_status())
            elif self.path == "/assistant/snapshot":
                self._reply_bytes(200, runtime.assistant_snapshot().encode("utf-8"), "application/json")
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
            elif self.path == "/warm":
                threading.Thread(
                    target=runtime.warm, name="InterfayceVoiceWarm", daemon=True
                ).start()
                self._reply(200, "warming")
            elif self.path == "/listen/assistant":
                self._reply(200, runtime.assistant_command())
            elif self.path == "/assistant/clear":
                self._reply(200, runtime.clear_assistant())
            elif self.path == "/assistant/cancel":
                self._reply(200, runtime.cancel_assistant())
            elif self.path == "/assistant/select":
                self._reply(200, runtime.select_assistant())
            elif self.path.startswith("/assistant/choice/"):
                try:
                    token, choice = self.path.rsplit("/", 2)[-2:]
                    runtime.assistant_session.codex.choose(token, int(choice))
                except (ValueError, RuntimeError) as error:
                    self._reply(409, str(error))
                else:
                    self._reply(200, runtime.assistant_snapshot())
            elif self.path.startswith("/assistant/answer/"):
                token = self.path.rsplit("/", 1)[-1]
                threading.Thread(target=runtime.dictate_assistant_answer, args=(token,), daemon=True).start()
                self._reply(200, "listening")
            elif self.path.startswith("/music/control/"):
                operation = self.path.rsplit("/", 1)[-1]
                self._reply(200, "ok" if runtime.music_control(operation) else "unavailable")
            elif self.path == "/comms/toggle":
                self._reply(200, runtime.toggle_comms())
            elif self.path == "/comms/start":
                self._reply(200, runtime.start_comms())
            elif self.path == "/comms/stop":
                self._reply(200, runtime.stop_comms())
            elif self.path == "/comms/clear":
                self._reply(200, runtime.clear_comms())
            elif self.path.startswith("/comms/shortcut/"):
                try:
                    index = int(self.path.rsplit("/", 1)[-1])
                except ValueError:
                    self._reply(400, "ERROR\tInvalid shortcut index.")
                else:
                    self._reply(200, runtime.send_comms_shortcut(index))
            elif self.path == "/desktop/recent":
                try:
                    length = max(0, min(int(self.headers.get("Content-Length", "0")), 2048))
                except ValueError:
                    length = 0
                label, separator, executable = self.rfile.read(length).decode(
                    "utf-8", errors="replace").partition("\t")
                try:
                    if not separator:
                        raise ValueError("Desktop recent payload is malformed.")
                    record_desktop_recent(label, executable)
                except ValueError:
                    self._reply(400, "invalid desktop recent")
                else:
                    self._reply(200, desktop_favorites_wire_text())
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
                    LOGGER.info("Battery announcement queued: %s", announcement)
                    speak_in_background(announcement)
                self._reply(200, announcement or "quiet")
            elif self.path == "/settings/tts/volume/up":
                self._reply(200, settings_wire_text(adjust_tts_volume(0.1)))
            elif self.path == "/settings/tts/volume/down":
                self._reply(200, settings_wire_text(adjust_tts_volume(-0.1)))
            elif self.path == "/settings/tts/mute/toggle":
                self._reply(200, settings_wire_text(toggle_tts_mute()))
            elif self.path == "/settings/song-announce/toggle":
                self._reply(200, settings_wire_text(toggle_song_announce()))
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

    server = BoundedVoiceServer(("127.0.0.1", port), Handler)
    runtime.health.start()
    threading.Thread(target=runtime.restore_assistant, name='AssistantRestore', daemon=True).start()
    runtime.chatbox.start()
    runtime.song_announcer.start()
    LOGGER.info("Spotify OSC song announcer started")
    if warm:
        threading.Thread(target=runtime.warm, name="InterfayceParakeetWarm", daemon=True).start()
    try:
        server.serve_forever()
    finally:
        runtime.assistant_session.close()
        runtime.health.stop()
        runtime.song_announcer.stop()
        runtime.chatbox.stop()
        LOGGER.info("Voice service stopping")
        server.server_close()


def configured_port() -> int:
    return int(os.environ.get("INTERFAYCE_VOICE_PORT", str(DEFAULT_PORT)))
