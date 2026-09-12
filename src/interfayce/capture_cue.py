"""Small local earcons that bracket an armed voice capture."""

from __future__ import annotations

from array import array
import logging
import math
import os
import threading

from .kokoro import configured_output_device_index


LOGGER = logging.getLogger("interfayce.voice")


def cue_pcm(starting: bool, *, sample_rate: int = 24_000) -> bytes:
    """Build a quiet, click-free rising or falling mono 16-bit cue."""

    duration = 0.090 if starting else 0.075
    start_frequency, end_frequency = ((620.0, 880.0) if starting else (760.0, 500.0))
    frame_count = max(2, round(sample_rate * duration))
    phase = 0.0
    samples = array("h")
    for index in range(frame_count):
        progress = index / (frame_count - 1)
        frequency = start_frequency + (end_frequency - start_frequency) * progress
        phase += 2.0 * math.pi * frequency / sample_rate
        # A sine envelope removes hard edges; keep the short cue clearly audible.
        envelope = math.sin(math.pi * progress) ** 2
        samples.append(round(math.sin(phase) * envelope * 0.085 * 32_767))
    return samples.tobytes()


_OUTPUT_LOCK = threading.Lock()


def play_capture_cue(starting: bool) -> None:
    _play_pcm(cue_pcm(starting))


def play_result_cue(sent: bool) -> None:
    # Two rising notes mean sent; one low note means nothing usable was heard.
    if sent:
        pcm = cue_pcm(True) + bytes(24000 // 20 * 2) + cue_pcm(True)
    else:
        pcm = cue_pcm(False)
    _play_pcm(pcm)


def _play_pcm(pcm: bytes) -> None:
    with _OUTPUT_LOCK:
        _play_pcm_locked(pcm)


def _play_pcm_locked(pcm: bytes) -> None:
    """Play one cue locally; failure must never block voice capture."""

    if os.environ.get("INTERFAYCE_CAPTURE_CUES", "on").casefold() in {
            "0", "false", "off", "no"}:
        return
    try:
        import pyaudio  # type: ignore[import-not-found]

        audio = pyaudio.PyAudio()
        try:
            output_index = configured_output_device_index(audio)
            stream = audio.open(
                format=pyaudio.paInt16,
                channels=1,
                rate=24_000,
                output=True,
                output_device_index=output_index,
            )
            try:
                # Match the working speech rate and allow short cues to clear
                # the output buffer before closing the stream.
                stream.write(pcm + bytes(24_000 // 4 * 2))
            finally:
                try:
                    stream.stop_stream()
                finally:
                    stream.close()
        finally:
            audio.terminate()
    except Exception:
        LOGGER.warning("Voice capture cue could not be played", exc_info=True)


def capture_with_cues(capture, *args, cue=play_capture_cue, end_async=False, **kwargs):
    """Keep reading microphone frames while the ready sound drains."""
    import threading

    worker = None
    def ready():
        nonlocal worker
        worker = threading.Thread(target=cue, args=(True,), daemon=True)
        worker.start()
    try:
        return capture(*args, on_ready=ready, **kwargs)
    finally:
        if worker is not None:
            def finish_cue():
                worker.join()
                cue(False)
            if end_async:
                threading.Thread(target=finish_cue, daemon=True).start()
            else:
                finish_cue()
