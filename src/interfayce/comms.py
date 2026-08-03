"""Continuous, explicitly armed VRChat chatbox dictation."""

from __future__ import annotations

from dataclasses import dataclass
import logging
import threading
from typing import Callable, Protocol

from .osc import VrchatOscClient
from .parakeet_stt import capture_microphone_once


LOGGER = logging.getLogger("interfayce.voice")


class Transcriber(Protocol):
    def transcribe(self, audio: object) -> str: ...


@dataclass(frozen=True, slots=True)
class CommsSnapshot:
    state: str
    transcript: str = ""

    @property
    def listening(self) -> bool:
        return self.state in {"LISTENING", "SENT", "STOPPING"}

    def wire_text(self) -> str:
        safe = self.transcript.replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()
        return f"{self.state}\t{safe}"


class CommsDictation:
    """Runs bounded utterance capture repeatedly until explicitly stopped."""

    def __init__(
        self,
        transcriber: Transcriber,
        command_lock: threading.Lock,
        *,
        capture: Callable[..., object] = capture_microphone_once,
        osc: VrchatOscClient | None = None,
    ) -> None:
        self._transcriber = transcriber
        self._command_lock = command_lock
        self._capture = capture
        self._osc = osc or VrchatOscClient()
        self._state_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._snapshot = CommsSnapshot("IDLE")

    def snapshot(self) -> CommsSnapshot:
        with self._state_lock:
            return self._snapshot

    def _set_snapshot(self, state: str, transcript: str = "") -> None:
        with self._state_lock:
            self._snapshot = CommsSnapshot(state, transcript)

    def toggle(self) -> CommsSnapshot:
        with self._state_lock:
            thread = self._thread
            current = self._snapshot
            if thread is not None and thread.is_alive():
                if not self._stop.is_set():
                    self._stop.set()
                    self._snapshot = CommsSnapshot("STOPPING", current.transcript)
                return self._snapshot

            self._stop.clear()
            self._snapshot = CommsSnapshot("LISTENING")
            self._thread = threading.Thread(
                target=self._run, name="InterfayceCommsDictation", daemon=True)
            self._thread.start()
            return self._snapshot

    def clear(self) -> CommsSnapshot:
        self._osc.clear_chatbox()
        current = self.snapshot()
        state = "LISTENING" if current.listening and current.state != "STOPPING" else "CLEARED"
        self._set_snapshot(state)
        LOGGER.info("Comms chatbox clear pulse sent")
        return self.snapshot()

    def _run(self) -> None:
        if not self._command_lock.acquire(blocking=False):
            self._set_snapshot("ERROR", "Voice capture is already active.")
            return
        LOGGER.info("Comms dictation started")
        try:
            first_capture = True
            while not self._stop.is_set():
                try:
                    audio = self._capture(
                        timeout_seconds=1.0,
                        phrase_seconds=6.0,
                        ambient_seconds=0.2 if first_capture else 0.05,
                    )
                    first_capture = False
                except Exception as error:
                    # Silence is expected while armed; SpeechRecognition reports it
                    # as WaitTimeoutError after the short listening window.
                    if error.__class__.__name__ == "WaitTimeoutError":
                        continue
                    LOGGER.exception("Comms microphone capture failed")
                    self._set_snapshot("ERROR", str(error))
                    return
                if self._stop.is_set():
                    break
                try:
                    transcript = self._transcriber.transcribe(audio).strip()
                except Exception as error:
                    LOGGER.exception("Comms transcription failed")
                    self._set_snapshot("ERROR", str(error))
                    return
                if not transcript:
                    continue
                transcript = transcript[:144]
                try:
                    self._osc.send_chatbox_message(transcript)
                except Exception as error:
                    LOGGER.exception("Comms OSC send failed")
                    self._set_snapshot("ERROR", str(error))
                    return
                LOGGER.info("Comms transcript sent: %r", transcript)
                self._set_snapshot("SENT", transcript)
        finally:
            self._command_lock.release()
            if self.snapshot().state != "ERROR":
                self._set_snapshot("IDLE", self.snapshot().transcript)
            LOGGER.info("Comms dictation stopped")
