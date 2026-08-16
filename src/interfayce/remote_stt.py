"""OpenAI-compatible remote STT adapter with local fallback."""

from __future__ import annotations

import json
from io import BytesIO
import logging
import secrets
from pathlib import Path
import mimetypes
import re
import wave
from typing import Protocol
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from .secure_store import delete_secret, read_secret, write_secret


LOGGER = logging.getLogger("interfayce.voice")
_SECRET_NAME = "remote_stt_api_key"


class Transcriber(Protocol):
    def warm(self) -> None: ...
    def transcribe(self, audio: object) -> str: ...


def set_remote_stt_api_key(value: str) -> None:
    cleaned = value.strip()
    if not cleaned:
        raise ValueError("Remote STT API key cannot be blank.")
    write_secret(_SECRET_NAME, cleaned.encode("utf-8"))


def load_remote_stt_api_key() -> str | None:
    value = read_secret(_SECRET_NAME)
    return value.decode("utf-8") if value else None


def delete_remote_stt_api_key() -> bool:
    return delete_secret(_SECRET_NAME)


def valid_remote_stt_endpoint(value: str) -> bool:
    parsed = urlparse(value.strip())
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


def transcription_endpoint(value: str) -> str:
    cleaned = value.strip().rstrip("/")
    if cleaned.endswith("/v1/audio/transcriptions"):
        return cleaned
    return cleaned + "/v1/audio/transcriptions"


def _multipart(wav_data: bytes, model: str) -> tuple[bytes, str]:
    boundary = "----Interfayce" + secrets.token_hex(16)
    body = b"".join((
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n{model}\r\n".encode(),
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n".encode(),
        wav_data,
        f"\r\n--{boundary}--\r\n".encode(),
    ))
    return body, f"multipart/form-data; boundary={boundary}"


def _warmup_wav() -> bytes:
    """Return a short, valid silent WAV that forces the STT model through inference."""
    output = BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16_000)
        wav.writeframes(b"\0" * (16_000 // 2))
    return output.getvalue()


def transcribe_audio_file(path: str | Path, endpoint: str, model: str,
                          *, timeout_seconds: float = 90.0) -> str:
    """Send a bounded audio file directly to the configured OpenAI-style STT server."""
    audio_path = Path(path)
    allowed = {".wav", ".mp3", ".m4a", ".flac", ".ogg", ".webm"}
    if not audio_path.is_file() or audio_path.suffix.casefold() not in allowed:
        raise ValueError("Assistant audio must be WAV, MP3, M4A, FLAC, OGG, or WebM.")
    size = audio_path.stat().st_size
    if not 1 <= size <= 25 * 1024 * 1024:
        raise ValueError("Assistant audio must be between 1 byte and 25 MiB.")
    if not valid_remote_stt_endpoint(endpoint):
        raise ValueError("Remote STT endpoint is invalid.")
    key = load_remote_stt_api_key()
    if not key:
        raise RuntimeError("Remote STT API key is not configured.")
    boundary = "----Interfayce" + secrets.token_hex(16)
    safe_name = re.sub(r"[^A-Za-z0-9._-]", "_", audio_path.name)[:120]
    content_type = mimetypes.guess_type(audio_path.name)[0] or "application/octet-stream"
    body = b"".join((
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n{model.strip() or 'whisper-turbo'}\r\n".encode(),
        (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
         f"filename=\"{safe_name}\"\r\nContent-Type: {content_type}\r\n\r\n").encode(),
        audio_path.read_bytes(),
        f"\r\n--{boundary}--\r\n".encode(),
    ))
    headers = {
        "Authorization": f"Bearer {key}",
        "Content-Type": f"multipart/form-data; boundary={boundary}",
    }
    request = Request(transcription_endpoint(endpoint), data=body, headers=headers, method="POST")
    with urlopen(request, timeout=timeout_seconds) as response:
        payload = json.load(response)
    text = str(payload.get("text", "")).strip()
    if not text:
        raise RuntimeError("Remote STT returned an empty transcript.")
    return text


class RemoteSttTranscriber:
    def __init__(self, endpoint: str, model: str, fallback: Transcriber | None = None,
                 timeout_seconds: float = 30.0):
        if not valid_remote_stt_endpoint(endpoint):
            raise ValueError("Remote STT endpoint is invalid.")
        self.endpoint = transcription_endpoint(endpoint)
        self.health_endpoint = endpoint.strip().rstrip("/")
        if self.health_endpoint.endswith("/v1/audio/transcriptions"):
            self.health_endpoint = self.health_endpoint[:-len("/v1/audio/transcriptions")]
        self.health_endpoint += "/health"
        self.model = model.strip() or "whisper-turbo"
        self.fallback = fallback
        self.timeout_seconds = timeout_seconds

    @property
    def description(self) -> str:
        return f"remote {self.endpoint} model={self.model}"

    def _headers(self) -> dict[str, str]:
        key = load_remote_stt_api_key()
        if not key:
            raise RuntimeError("Remote STT API key is not configured.")
        return {"Authorization": f"Bearer {key}"}

    def warm(self) -> None:
        try:
            body, content_type = _multipart(_warmup_wav(), self.model)
            headers = self._headers()
            headers["Content-Type"] = content_type
            request = Request(self.endpoint, data=body, headers=headers, method="POST")
            with urlopen(request, timeout=self.timeout_seconds) as response:
                if response.status != 200:
                    raise RuntimeError(f"Remote STT warm-up returned HTTP {response.status}.")
                response.read()
        except Exception:
            if self.fallback is None:
                raise
            LOGGER.warning("Remote STT warm-up failed; warming local fallback", exc_info=True)
            self.fallback.warm()

    def transcribe(self, audio: object) -> str:
        try:
            wav_data = audio.get_wav_data(convert_rate=16_000, convert_width=2)
            body, content_type = _multipart(wav_data, self.model)
            headers = self._headers()
            headers["Content-Type"] = content_type
            request = Request(self.endpoint, data=body, headers=headers, method="POST")
            with urlopen(request, timeout=self.timeout_seconds) as response:
                payload = json.load(response)
            text = str(payload.get("text", "")).strip()
            if not text:
                raise RuntimeError("Remote STT returned an empty transcript.")
            return text
        except Exception:
            if self.fallback is None:
                raise
            LOGGER.warning("Remote STT failed; using local Parakeet fallback", exc_info=True)
            return self.fallback.transcribe(audio)
