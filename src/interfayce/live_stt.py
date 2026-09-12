"""Background streaming client and conservative caption formatting."""
import json
import logging
import queue
import re
import threading
import time
from urllib.request import Request, urlopen

LOGGER = logging.getLogger("interfayce.voice")
_TOKENS = re.compile(r"<unk>|<blank>|<pad>|</?s>|<sil>|<\|[^<>]*\|>", re.I)


def clean_transcript(text):
    text = " ".join(_TOKENS.sub("", str(text)).split())
    return text if any(character.isalnum() for character in text) else ""


def caption_tail(text, limit=144):
    """Keep the newest whole words, rather than freezing at the first 144."""
    text = clean_transcript(text)
    if len(text) <= limit:
        return text
    tail = text[-(limit - 1):]
    if " " in tail:
        tail = tail.split(" ", 1)[1]
    return "…" + tail


class LiveCaptionPublisher:
    def __init__(self, publish, clock=time.monotonic):
        self.publish = publish
        self.clock = clock
        self.previous = []
        self.last = ""
        self.next_update = 0.0

    def update(self, text, final=False):
        words = clean_transcript(text).split()
        if final:
            settled = words
        else:
            count = 0
            for old, new in zip(self.previous, words):
                if old != new:
                    break
                count += 1
            settled = words[:count]
        self.previous = words
        caption = caption_tail(" ".join(settled))
        if caption and (final or (caption != self.last and self.clock() >= self.next_update)):
            self.publish(caption)
            self.last = caption
            self.next_update = self.clock() + 1.0
        return caption


class RemoteLiveSession:
    """Only the worker resamples or uses the network; mic callbacks never wait."""
    def __init__(self, endpoint, headers, on_partial):
        self.base = endpoint.removesuffix("/v1/audio/transcriptions").rstrip("/") + "/v1/audio/streams"
        self.headers = headers
        self.on_partial = on_partial
        self.queue = queue.Queue(maxsize=2048)
        self.cancelled = threading.Event()
        self.done = threading.Event()
        self.error = None
        self.text = ""
        self.worker = threading.Thread(target=self._run, daemon=True, name="InterfayceLiveSTT")
        self.worker.start()

    def _request(self, method, url, body=None):
        headers = dict(self.headers)
        headers["Content-Type"] = "application/octet-stream"
        with urlopen(Request(url, data=body, headers=headers, method=method), timeout=5.0) as response:
            return json.load(response)

    def push(self, pcm, sample_rate, sample_width):
        if not self.done.is_set() and not self.cancelled.is_set():
            try:
                self.queue.put_nowait((pcm, sample_rate, sample_width))
            except queue.Full:
                self.error = RuntimeError("Streaming audio queue filled")
                self.cancelled.set()

    def finish(self):
        if not self.done.is_set():
            try:
                self.queue.put_nowait(None)
            except queue.Full:
                self.cancelled.set()
            if not self.done.wait(6.0):
                self.cancelled.set()
                raise TimeoutError("Streaming finalization timed out")
        if self.error:
            raise self.error
        return self.text

    def close(self):
        self.cancelled.set()

    def _run(self):
        import audioop
        session_url = None
        try:
            session = self._request("POST", self.base, b"")
            session_url = self.base + "/" + session["session"]
            resample_state = None
            pending = bytearray()
            sequence = 0
            final = False
            while not self.cancelled.is_set():
                try:
                    item = self.queue.get(timeout=0.1)
                except queue.Empty:
                    continue
                if item is None:
                    final = True
                else:
                    pcm, rate, width = item
                    if width != 2:
                        pcm = audioop.lin2lin(pcm, width, 2)
                    pcm, resample_state = audioop.ratecv(pcm, 2, 1, rate, 16000, resample_state)
                    pending.extend(pcm)
                if len(pending) < 16000 and not final:
                    continue
                result = self._request("POST",
                    session_url + f"?sequence={sequence}&final={int(final)}", bytes(pending))
                sequence += 1
                pending.clear()
                self.text = clean_transcript(result.get("text", ""))
                if not self.cancelled.is_set() and not final:
                    self.on_partial(self.text)
                if final:
                    session_url = None  # Server closed it.
                    break
        except Exception as error:
            self.error = error
            LOGGER.warning("Live STT unavailable; retaining recording for fallback: %s", type(error).__name__)
        finally:
            # Unblock release promptly; cleanup may take another network timeout.
            self.done.set()
            if session_url:
                try:
                    self._request("DELETE", session_url)
                except Exception:
                    pass
