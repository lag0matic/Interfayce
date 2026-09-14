"""Cached live controls for a currently playing utterance."""
import time
from .settings import load_settings


class PlaybackControls:
    def __init__(self, source=load_settings, clock=time.monotonic):
        self._source, self._clock = source, clock
        self._next_check = 0
        self._current = None

    def current(self):
        now = self._clock()
        if self._current is None or now >= self._next_check:
            self._current = self._source()
            self._next_check = now + 0.1
        return self._current
