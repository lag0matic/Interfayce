"""Serialize personal chat and lower-priority, replaceable song announcements."""
from __future__ import annotations

import logging
import threading
import time

from .osc import VrchatOscClient
from .caption_pages import CaptionPages

LOGGER = logging.getLogger("interfayce.voice")


class ChatboxCoordinator:
    def __init__(self, osc=None, *, clock=time.monotonic, song_enabled=lambda: True,
                 chat_seconds=30.0, song_seconds=7.0):
        self._osc = osc or VrchatOscClient()
        self._clock = clock
        self._song_enabled = song_enabled
        self._chat_seconds = chat_seconds
        self._song_seconds = song_seconds
        self._lock = threading.RLock()
        self._until = 0.0
        self._owner = None
        self._pending = None
        self._busy = False
        self._stop = threading.Event()
        self._thread = None
        self._captions = CaptionPages()
        self._caption_mode = False
        self._chat_text = ""
        self._page_characters = 0

    def send_chatbox_message(self, text):
        with self._lock:
            # Explicit shortcuts replace both visible and queued dictation.
            self._captions = CaptionPages()
            self._caption_mode = False
            self._osc.send_chatbox_message(text)
            self._chat_text = text
            self._page_characters = len(text)
            self._owner = "chat"
            self._until = self._clock() + self._chat_seconds

    def begin_dictation(self):
        with self._lock:
            self.tick()
            if not self._caption_mode:
                self._captions = CaptionPages()
                if self._owner == "chat" and self._clock() < self._until:
                    item = self._captions.begin()
                    self._captions.update(item, self._chat_text, final=True)
                    self._captions.page()
                self._caption_mode = True
            return self._captions.begin()

    def update_dictation(self, token, text, *, final=False):
        with self._lock:
            self.tick()
            if not self._caption_mode or not self._captions.update(token, text, final=final):
                return
            self._render_captions()

    def dictation_queued(self):
        with self._lock:
            return self._caption_mode and self._captions.queued()

    def _render_captions(self):
        text = self._captions.page()
        if not text:
            if self._owner == "chat":
                self._osc.clear_chatbox()
                self._owner = None
                self._until = 0.0
                self._chat_text = ""
            return
        if self._owner != "chat" or text != self._chat_text:
            self._osc.send_chatbox_message(text)
            if self._owner != "chat":
                self._until = self._clock() + self._chat_seconds
                self._page_characters = len(text)
            else:
                # Budget only newly visible growth, not repeated hypotheses or
                # corrections back to a previously displayed page length.
                added = max(0, len(text) - self._page_characters)
                if added:
                    now = self._clock()
                    reading_seconds = added / 15.0
                    self._until = min(now + self._chat_seconds,
                                      max(self._until + reading_seconds,
                                          now + 3.0 + reading_seconds))
                    self._page_characters = len(text)
            self._owner = "chat"
            self._chat_text = text

    def clear_chatbox(self):
        with self._lock:
            self._captions = CaptionPages()
            self._caption_mode = False
            self._chat_text = ""
            self._osc.clear_chatbox()
            self._owner = None
            self._until = 0.0

    def set_typing(self, active):
        self._osc.set_typing(active)

    def set_busy(self, active):
        with self._lock:
            self._busy = active

    def announce_song(self, text):
        with self._lock:
            self._pending = text if self._song_enabled() else None
            self.tick()

    def tick(self):
        with self._lock:
            if self._pending is not None and not self._song_enabled():
                self._pending = None
            now = self._clock()
            if now < self._until:
                return
            if self._caption_mode and self._owner == "chat":
                self._captions.advance()
                self._owner = None
                self._chat_text = ""
                self._until = 0.0
                self._render_captions()
                if self._owner == "chat":
                    return
                # Appending sends OSC again, which can restart VRChat's own
                # timer; explicitly enforce our reading-time deadline.
                self._osc.clear_chatbox()
            # Only our own song expires via an explicit clear. Personal chat
            # uses VRChat's configured 30-second lifetime.
            if self._owner == "song":
                self._osc.clear_chatbox()
                self._owner = None
            elif self._owner == "chat":
                self._owner = None
                self._chat_text = ""
            if self._busy or self._pending is None:
                return
            self._osc.send_chatbox_message(self._pending)
            LOGGER.info("Dispatched queued Spotify announcement: chars=%s", len(self._pending))
            self._pending = None
            self._owner = "song"
            self._until = now + self._song_seconds

    def start(self):
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True,
                                        name="InterfayceChatbox")
        self._thread.start()

    def _run(self):
        while not self._stop.wait(0.1):
            try:
                self.tick()
            except Exception:
                LOGGER.exception("Chatbox scheduling failed")

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
