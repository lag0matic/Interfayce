"""Continuous, explicitly armed VRChat chatbox dictation."""

from __future__ import annotations

from dataclasses import dataclass
import logging
import threading
import time
from typing import Callable, Protocol

from .capture_cue import capture_with_cues
from .live_stt import clean_transcript, LiveCaptionPublisher
from .osc import VrchatOscClient, fit_chatbox_text
from .parakeet_stt import capture_microphone_until


LOGGER = logging.getLogger("interfayce.voice")


class Transcriber(Protocol):
    def transcribe(self, audio: object) -> str: ...


@dataclass(frozen=True, slots=True)
class CommsSnapshot:
    state: str
    transcript: str = ""

    @property
    def listening(self) -> bool:
        return self.state in {"LISTENING", "TRANSCRIBING", "STOPPING"}

    def wire_text(self) -> str:
        safe = self.transcript.replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()
        return f"{self.state}\t{safe}"


class CommsDictation:
    """Records one complete push-to-talk message and sends it on release."""

    def __init__(
        self,
        transcriber: Transcriber,
        command_lock: threading.Lock,
        *,
        capture: Callable[..., object] = capture_microphone_until,
        osc: VrchatOscClient | None = None,
        cue: Callable[[bool], None] | None = None,
        result_cue: Callable[[bool], None] | None = None,
        silence_auto_stop_seconds: float = 3.0,
        merge_window_seconds: float = 0.9,
    ) -> None:
        self._transcriber = transcriber
        self._command_lock = command_lock
        self._capture = capture
        self._osc = osc or VrchatOscClient()
        self._cue = cue or (lambda _starting: None)
        self._result_cue = result_cue
        self._publish_lock = threading.RLock()
        self._suppress_live = False
        # Accepted for settings/backward compatibility. Push-to-talk is bounded
        # by button release and a hard safety maximum, not silence detection.
        del silence_auto_stop_seconds, merge_window_seconds
        self._state_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._snapshot = CommsSnapshot("IDLE")

    def snapshot(self) -> CommsSnapshot:
        with self._state_lock:
            return self._snapshot

    def set_silence_auto_stop_seconds(self, seconds: float) -> None:
        del seconds

    def _set_snapshot(self, state: str, transcript: str = "") -> None:
        with self._state_lock:
            self._snapshot = CommsSnapshot(state, transcript)

    def start(self) -> CommsSnapshot:
        with self._state_lock:
            thread = self._thread
            if thread is not None and thread.is_alive():
                return self._snapshot

            self._suppress_live = False
            self._stop.clear()
            self._snapshot = CommsSnapshot("LISTENING")
            self._thread = threading.Thread(
                target=self._run, name="InterfayceCommsDictation", daemon=True)
            self._thread.start()
            return self._snapshot

    def stop(self) -> CommsSnapshot:
        with self._state_lock:
            thread = self._thread
            if thread is None or not thread.is_alive():
                return self._snapshot
            self._stop.set()
            self._snapshot = CommsSnapshot("TRANSCRIBING", self._snapshot.transcript)
            snapshot = self._snapshot
        # Clear presence at button-up rather than leaving the typing bubble on
        # throughout remote transcription latency.
        self._set_typing(False)
        return snapshot

    def toggle(self) -> CommsSnapshot:
        """Compatibility wrapper for older overlay builds."""
        thread = self._thread
        return self.stop() if thread is not None and thread.is_alive() else self.start()

    def clear(self) -> CommsSnapshot:
        with self._publish_lock:
            self._suppress_live = True
            self._osc.clear_chatbox()
        current = self.snapshot()
        state = current.state if current.listening else "CLEARED"
        self._set_snapshot(state)
        LOGGER.info("Comms chatbox clear pulse sent")
        return self.snapshot()

    def send_shortcut(self, message: str) -> CommsSnapshot:
        text = fit_chatbox_text(message)
        if not text:
            raise ValueError("Comms shortcut is empty.")
        with self._publish_lock:
            self._suppress_live = True
            self._osc.send_chatbox_message(text)
        thread = self._thread
        listening = thread is not None and thread.is_alive() and not self._stop.is_set()
        self._set_snapshot("LISTENING" if listening else "SHORTCUT", text)
        LOGGER.info("Comms shortcut sent: chars=%s", len(text))
        return self.snapshot()

    def _run(self) -> None:
        if not self._command_lock.acquire(blocking=False):
            self._set_snapshot("ERROR", "Voice capture is already active.")
            return
        LOGGER.info("Comms push-to-talk capture started")
        live = None
        publisher = None

        def publish(text):
            with self._publish_lock:
                if not self._suppress_live:
                    self._osc.send_chatbox_message(text)
                    self._set_snapshot("TRANSCRIBING" if self._stop.is_set() else "LISTENING", text)

        try:
            if hasattr(self._osc, "set_busy"):
                self._osc.set_busy(True)
            self._set_typing(True)
            if hasattr(self._transcriber, "start_stream"):
                publisher = LiveCaptionPublisher(publish)
                try:
                    live = self._transcriber.start_stream(publisher.update)
                except Exception:
                    LOGGER.warning("Could not start live STT; using complete recording", exc_info=True)
            capture_options = {"on_chunk": live.push} if live is not None else {}
            audio = capture_with_cues(
                self._capture, self._stop, max_seconds=30.0, cue=self._cue,
                end_async=self._result_cue is not None, **capture_options)
            released = time.monotonic()
            self._set_snapshot("TRANSCRIBING")
            if hasattr(audio, "frame_data") and not audio.frame_data:
                self._set_snapshot("IDLE")
                return
            if live is not None:
                try:
                    transcript = live.finish()
                except Exception:
                    live.close()
                    LOGGER.warning("Finishing chat through whole-recording fallback")
                    transcript = self._transcriber.transcribe(audio)
            else:
                transcript = self._transcriber.transcribe(audio)
            transcript = clean_transcript(transcript)
            with self._publish_lock:
                if self._suppress_live:
                    self._set_snapshot("CLEARED")
                    return
                if transcript:
                    if publisher is not None:
                        transcript = publisher.update(transcript, final=True)
                    else:
                        transcript = fit_chatbox_text(transcript)
                        self._osc.send_chatbox_message(transcript)
                    LOGGER.info("Comms transcript sent: chars=%s release_to_send=%.3fs live=%s",
                                len(transcript), time.monotonic() - released, live is not None)
                    self._set_snapshot("SENT", transcript)
                else:
                    if publisher is not None and publisher.last:
                        self._osc.clear_chatbox()
                    self._set_snapshot("IDLE")
            if self._result_cue is not None:
                threading.Thread(target=self._result_cue, args=(bool(transcript),),
                                 daemon=True, name="InterfayceCommsResultCue").start()
        except Exception as error:
            LOGGER.exception("Comms push-to-talk failed")
            self._set_snapshot("ERROR", str(error))
        finally:
            if live is not None:
                live.close()
            self._set_typing(False)
            if hasattr(self._osc, "set_busy"):
                self._osc.set_busy(False)
            self._command_lock.release()
            LOGGER.info("Comms push-to-talk stopped")

    def _set_typing(self, is_typing: bool) -> None:
        try:
            self._osc.set_typing(is_typing)
        except Exception:
            # Typing presence is useful feedback, never a reason to lose speech.
            LOGGER.warning("Could not update VRChat typing indicator", exc_info=True)
