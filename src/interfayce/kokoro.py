"""Serialized speech queue with Ming streaming and selectable Kokoro playback."""

from __future__ import annotations

from collections import deque
from io import BytesIO
import json
import logging
import os
import threading
import time
import urllib.request
import wave
import re

from .settings import load_settings
from .playback_controls import PlaybackControls


LOGGER = logging.getLogger("interfayce.voice")
_CONDITION = threading.Condition()
_pending_speech: deque[tuple[str, threading.Event | None]] = deque()
_worker_started = False
_SPEECH_LOCK = threading.Lock()


def synthesize(text: str, *, timeout_seconds: float = 30.0) -> bytes:
    settings = load_settings()
    endpoint = os.environ.get("INTERFAYCE_KOKORO_URL", settings.tts_endpoint).strip()
    model = os.environ.get("INTERFAYCE_KOKORO_MODEL", settings.tts_model).strip()
    voice = os.environ.get("INTERFAYCE_KOKORO_VOICE", settings.tts_voice).strip()
    if not endpoint or not model or not voice:
        raise RuntimeError("Kokoro endpoint, model, and voice must be configured in Settings.")
    payload = json.dumps({
        "model": model,
        "input": strip_delivery_tags(text),
        "voice": voice,
        "response_format": "wav",
        "speed": float(os.environ.get("INTERFAYCE_KOKORO_SPEED", str(settings.tts_speed))),
    }).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=payload,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    started = time.monotonic()
    LOGGER.info("Kokoro synthesis started: chars=%s", len(text))
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        result = response.read()
    LOGGER.info("Kokoro synthesis completed in %.2fs", time.monotonic() - started)
    return result


def configured_output_device_index(audio: object) -> int | None:
    configured = os.environ.get(
        "INTERFAYCE_TTS_OUTPUT", load_settings().tts_output).strip()
    if not configured:
        return None
    if configured.isdigit():
        return int(configured)
    wanted = configured.casefold()
    matches: list[tuple[int, int, int]] = []
    for index in range(audio.get_device_count()):
        info = audio.get_device_info_by_index(index)
        if info.get("maxOutputChannels", 0) <= 0:
            continue
        name = str(info.get("name", "")).casefold()
        host = audio.get_host_api_info_by_index(info["hostApi"])
        is_mme = host.get("type") == 2  # PortAudio paMME
        # Windows MME truncates endpoint names at 31 characters. A full name
        # saved by Settings previously skipped MME and selected DirectSound,
        # which accepts writes but drops playback on some headset drivers.
        truncated_match = is_mme and len(name) == 31 and wanted.startswith(name)
        if name == wanted or truncated_match or wanted in name:
            identity_rank = 0 if name == wanted or truncated_match else 1
            matches.append((identity_rank, 0 if is_mme else 1, index))
    return min(matches)[2] if matches else None


def play_wav(wav_bytes: bytes, *, cancel: threading.Event | None = None) -> None:
    controls = PlaybackControls(source=load_settings)
    settings = controls.current()
    if settings.tts_muted or settings.tts_volume <= 0.0:
        LOGGER.info("Kokoro acknowledgment muted")
        return

    import audioop
    import pyaudio  # type: ignore[import-not-found]

    with wave.open(BytesIO(wav_bytes), "rb") as wav:
        audio = pyaudio.PyAudio()
        try:
            output_index = configured_output_device_index(audio)
            output_info = (audio.get_device_info_by_index(output_index)
                if output_index is not None else audio.get_default_output_device_info())
            LOGGER.info("Playing acknowledgment through output=%r index=%r",
                output_info.get("name"), output_index)
            stream = audio.open(
                format=audio.get_format_from_width(wav.getsampwidth()),
                channels=wav.getnchannels(),
                rate=wav.getframerate(),
                output=True,
                output_device_index=output_index,
            )
            try:
                while frames := wav.readframes(4096):
                    if cancel is not None and cancel.is_set():
                        break
                    settings = controls.current()
                    if settings.tts_muted or settings.tts_volume <= 0:
                        break
                    if settings.tts_volume < 1.0:
                        frames = audioop.mul(frames, wav.getsampwidth(), settings.tts_volume)
                    stream.write(frames)
            finally:
                try:
                    stream.stop_stream()
                finally:
                    stream.close()
        finally:
            audio.terminate()


def _speech_worker() -> None:
    while True:
        with _CONDITION:
            while not _pending_speech:
                _CONDITION.wait()
            text, cancel = _pending_speech.popleft()
        try:
            speak(text, cancel=cancel)
        except Exception:
            LOGGER.exception("Speech acknowledgment failed")


def strip_delivery_tags(text: str) -> str:
    return re.sub(r"\[(?:voice:[^\]]+|chuckle|laugh|sigh|gasp|groan|hmm|hmph|cough|clear throat|uhm|shush|shh|question ah|question huh)\]", "", text, flags=re.I).strip()


def speak(text: str, *, cancel=None) -> None:
    # Harness and queued speech share this gate: one TTS request at a time.
    with _SPEECH_LOCK:
        settings = load_settings()
        if settings.tts_muted or settings.tts_volume <= 0 or (cancel and cancel.is_set()):
            return
        if settings.tts_provider == "ming":
            from .ming_tts import stream_speech
            stream_speech(text, cancel=cancel)
        else:
            wav = synthesize(text)
            if cancel is None:
                play_wav(wav)
            else:
                play_wav(wav, cancel=cancel)


def speak_in_background(text: str, *, cancel: threading.Event | None = None) -> None:
    global _worker_started
    if os.environ.get("INTERFAYCE_TTS", "on").casefold() in {"0", "false", "off", "no"}:
        return
    with _CONDITION:
        if cancel is not None and cancel.is_set():
            return
        if len(_pending_speech) >= 8:
            LOGGER.warning("Speech queue full; skipping new acknowledgment")
            return
        _pending_speech.append((text, cancel))
        if not _worker_started:
            threading.Thread(
                target=_speech_worker, name="InterfayceSpeech", daemon=True
            ).start()
            _worker_started = True
        _CONDITION.notify()
