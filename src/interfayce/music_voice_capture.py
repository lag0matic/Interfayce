"""Release-aware music requests, with a separate event for each press."""

from concurrent.futures import Future
import logging
import threading
from uuid import uuid4


class MusicVoiceCapture:
    def __init__(self, command):
        self._command = command
        self._lock = threading.Lock()
        self._session = None

    def start(self):
        with self._lock:
            if self._session is not None and not self._session[2].done():
                return None
            token, stop, result = uuid4().hex, threading.Event(), Future()
            self._session = (token, stop, result)
            threading.Thread(target=self._run, args=(stop, result),
                             name="InterfayceMusicCapture", daemon=True).start()
            return token

    def _run(self, stop, result):
        try:
            result.set_result(self._command(stop_event=stop))
        except Exception:
            logging.getLogger("interfayce.voice").exception("Music push-to-talk request failed")
            result.set_result("ERROR\t\tMusic voice request failed.")

    def stop(self, token):
        with self._lock:
            if self._session is None or self._session[0] != token:
                return False
            self._session[1].set()
            return True

    def result(self, token):
        with self._lock:
            if self._session is None or self._session[0] != token:
                return "ERROR\t\tMusic capture session expired."
            result = self._session[2]
        try:
            return result.result(timeout=175)
        except TimeoutError:
            self.stop(token)
            return "ERROR\t\tMusic voice request timed out."
