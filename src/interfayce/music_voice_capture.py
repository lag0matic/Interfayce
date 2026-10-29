"""Release-aware music requests, with a separate event for each press."""

from concurrent.futures import Future
import logging
import threading
from uuid import uuid4


class MusicVoiceCapture:
    def __init__(self, command, *, label="Music", result_timeout=175):
        self._command = command
        self._label = label
        self._result_timeout = result_timeout
        self._lock = threading.Lock()
        self._session = None

    def start(self, **options):
        with self._lock:
            if self._session is not None and not self._session[2].done():
                return None
            token, stop, result = uuid4().hex, threading.Event(), Future()
            self._session = (token, stop, result)
            threading.Thread(target=self._run, args=(stop, result, options),
                             name=f"Interfayce{self._label}Capture", daemon=True).start()
            return token

    def _run(self, stop, result, options):
        try:
            result.set_result(self._command(stop_event=stop, **options))
        except Exception:
            logging.getLogger("interfayce.voice").exception("%s push-to-talk request failed", self._label)
            result.set_result(f"ERROR\t\t{self._label} voice request failed.")

    def stop(self, token):
        with self._lock:
            if self._session is None or self._session[0] != token:
                return False
            self._session[1].set()
            return True

    def result(self, token):
        with self._lock:
            if self._session is None or self._session[0] != token:
                return f"ERROR\t\t{self._label} capture session expired."
            result = self._session[2]
        try:
            return result.result(timeout=self._result_timeout)
        except TimeoutError:
            self.stop(token)
            return f"ERROR\t\t{self._label} voice request timed out."

    def stop_current(self):
        with self._lock:
            if self._session is not None:
                self._session[1].set()
