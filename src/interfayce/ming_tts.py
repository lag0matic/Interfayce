"""Bounded, cancellable Ming PCM transport with one continuous audio clock."""
from __future__ import annotations

import http.client
import io
import json
import logging
import os
import socket
import threading
import time
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

from .settings import load_settings
from .secure_store import read_secret
from .playback_controls import PlaybackControls

LOGGER = logging.getLogger("interfayce.voice")
RATE = 24000
BYTES_PER_SECOND = RATE * 2


class _CancellableReader(io.RawIOBase):
    def __init__(self, sock, stop):
        super().__init__()
        self.sock, self.stop = sock, stop

    def readable(self):
        return True

    def readinto(self, target):
        deadline = time.monotonic() + 120
        while not self.stop.is_set():
            try:
                return self.sock.recv_into(target)
            except socket.timeout:
                # A short socket wait is only a cancellation checkpoint, never
                # a synthesis timeout or a reason to retry the request.
                if time.monotonic() >= deadline:
                    raise TimeoutError("Ming sent no data for 120 seconds")
        return 0


class _ResponseSocket:
    def __init__(self, sock, stop):
        self.sock, self.stop = sock, stop

    def makefile(self, mode):
        return io.BufferedReader(_CancellableReader(self.sock, self.stop))


def speech_url(endpoint: str) -> str:
    endpoint = endpoint.strip().rstrip("/")
    return endpoint if endpoint.endswith("/audio/speech") else endpoint + "/audio/speech"


def headers() -> dict[str, str]:
    key = os.environ.get("INTERFAYCE_MING_API_KEY")
    if key is None:
        key = (read_secret("ming-api-key") or b"interfayce").decode("utf-8")
    return {"Content-Type": "application/json", "Authorization": "Bearer " + key}


def check_ready(endpoint: str | None = None) -> None:
    parts = urlsplit(speech_url(endpoint or load_settings().ming_endpoint))
    url = f"{parts.scheme}://{parts.netloc}/health"
    with urlopen(Request(url, headers=headers()), timeout=3) as response:
        if response.status != 200 or json.loads(response.read(262144)).get("status") != "ok":
            raise RuntimeError("Ming is still warming up")


class PcmBuffer:
    """Four-second bounded buffer. HTTP boundaries never become sample boundaries."""
    def __init__(self, capacity=BYTES_PER_SECOND * 4):
        self.capacity = capacity
        self.condition = threading.Condition()
        self.data = bytearray()
        self.carry = b""
        self.eof = False
        self.error = None
        self.underruns = 0
        self.underrun_offsets = []
        self.total = 0

    def append(self, chunk, stop):
        chunk = self.carry + chunk
        size = len(chunk) // 2 * 2
        self.carry = chunk[size:]
        position = 0
        with self.condition:
            while position < size and not stop.is_set():
                room = self.capacity - len(self.data)
                if room < 2:
                    self.condition.wait(0.05)
                    continue
                count = min(room, size - position)
                self.data.extend(chunk[position:position + count])
                self.total += count
                position += count
                self.condition.notify_all()

    def finish(self, error=None):
        with self.condition:
            self.error = error or (ValueError("Ming returned an incomplete PCM sample") if self.carry else None)
            self.eof = True
            self.condition.notify_all()

    def take(self, size):
        with self.condition:
            count = min(size, len(self.data))
            result = bytes(self.data[:count])
            del self.data[:count]
            finished = self.eof and not self.data
            if count < size and not self.eof:
                self.underruns += 1
                if len(self.underrun_offsets) < 16:
                    self.underrun_offsets.append(self.total)
            self.condition.notify_all()
        return result + bytes(size - count), finished


def stream_speech(text: str, *, cancel=None, audio_factory=None, timeout=180.0):
    """One request, reader, bounded buffer and PortAudio stream per reply; no retries."""
    import audioop
    import pyaudio
    from .kokoro import configured_output_device_index

    settings = load_settings()
    if settings.tts_muted or settings.tts_volume <= 0 or (cancel and cancel.is_set()):
        return
    parts = urlsplit(speech_url(settings.ming_endpoint))
    if parts.scheme not in ("http", "https") or not parts.hostname:
        raise ValueError("Ming needs an HTTP or HTTPS base URL or speech endpoint")
    payload = json.dumps({"model": "ming-tts", "voice": "cassia", "input": text,
                          "response_format": "pcm", "speed": 1.0}, ensure_ascii=False).encode("utf-8")
    request_headers = headers()
    stop = threading.Event()
    connected = []
    buffer = PcmBuffer()
    started = time.monotonic()
    first_pcm_at = []
    LOGGER.info("Ming request start: chars=%d", len(text))

    def receive():
        connection_type = http.client.HTTPSConnection if parts.scheme == "https" else http.client.HTTPConnection
        connection = connection_type(parts.hostname, parts.port, timeout=5)
        try:
            connection.connect()
            LOGGER.info("Ming connected: elapsed=%.3f", time.monotonic()-started)
            sock = connection.sock
            connected.append(sock)
            sock.settimeout(120)
            if stop.is_set():
                return
            path = parts.path + (("?" + parts.query) if parts.query else "")
            connection.request("POST", path, payload, request_headers)
            sock.settimeout(0.2)
            response = http.client.HTTPResponse(_ResponseSocket(sock, stop), method="POST")
            with response:
                response.begin()
                content_type = response.getheader("Content-Type", "").split(";", 1)[0].lower()
                LOGGER.info("Ming headers: status=%d type=%s profile=%s elapsed=%.3f",
                            response.status, content_type, response.getheader("X-Ming-Profile"), time.monotonic()-started)
                if response.status != 200:
                    raise RuntimeError(f"Ming returned HTTP {response.status}")
                if content_type not in ("audio/pcm", "audio/raw", "application/octet-stream", "audio/L16".lower()):
                    raise RuntimeError(f"Unexpected Ming audio content type: {content_type}")
                if response.getheader("X-Ming-Sample-Rate", "24000") != "24000":
                    raise RuntimeError("Unexpected Ming sample rate")
                first = True
                while not stop.is_set():
                    chunk = response.read1(4096)
                    if not chunk:
                        break
                    if first:
                        first_pcm_at.append(time.monotonic())
                        LOGGER.info("Ming first PCM: elapsed=%.3f", time.monotonic()-started)
                        first = False
                    buffer.append(chunk, stop)
                LOGGER.info("Ming EOF: bytes=%d elapsed=%.3f cancelled=%s",
                            buffer.total, time.monotonic()-started, stop.is_set())
        except Exception as error:
            if not stop.is_set():
                buffer.finish(error)
        finally:
            connection.close()
            buffer.finish(buffer.error)

    reader = threading.Thread(target=receive, name="MingPCMReader", daemon=True)
    reader.start()
    audio = stream = None
    controls = PlaybackControls(source=load_settings)
    volume = [settings.tts_volume]
    try:
        def callback(in_data, frames, timing, status):
            if stop.is_set():
                return (b"", pyaudio.paAbort)
            pcm, finished = buffer.take(frames * 2)
            if volume[0] < 1:
                pcm = audioop.mul(pcm, 2, volume[0])
            return pcm, pyaudio.paComplete if finished else pyaudio.paContinue

        while True:
            live = controls.current()
            if (cancel and cancel.is_set()) or live.tts_muted or live.tts_volume <= 0:
                break
            if live.tts_output != settings.tts_output:
                LOGGER.info("Ming output selection changed; stopping current reply")
                break
            volume[0] = live.tts_volume
            if time.monotonic() - started > timeout:
                raise TimeoutError("Ming speech exceeded its overall timeout")
            with buffer.condition:
                if buffer.error:
                    raise buffer.error
                ready = buffer.eof or (len(buffer.data) >= BYTES_PER_SECOND // 5
                    and first_pcm_at and time.monotonic() - first_pcm_at[0] >= 0.2)
                empty = not buffer.data
            if stream is None and ready:
                if empty:
                    break
                audio = (audio_factory or pyaudio.PyAudio)()
                stream = audio.open(format=pyaudio.paInt16, channels=1, rate=RATE,
                                    output=True, output_device_index=configured_output_device_index(audio),
                                    frames_per_buffer=480, stream_callback=callback, start=False)
                LOGGER.info("Ming playback start: elapsed=%.3f", time.monotonic()-started)
                stream.start_stream()
            if stream is not None and not stream.is_active():
                break
            stop.wait(0.02)
    finally:
        stop.set()
        with buffer.condition:
            buffer.condition.notify_all()
        for sock in connected:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        try:
            if stream is not None:
                try:
                    stream.stop_stream()
                finally:
                    stream.close()
        finally:
            if audio is not None:
                audio.terminate()
            # Connection establishment has a five-second bound. Do not start
            # another request until this reader has actually exited.
            reader.join()
            LOGGER.info("Ming playback ended: bytes=%d underruns=%d underrun_offsets=%s",
                        buffer.total, buffer.underruns, buffer.underrun_offsets)
