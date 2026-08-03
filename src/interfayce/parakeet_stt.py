"""Local Parakeet STT adapter using Sherpa ONNX."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import threading
from typing import Any


DEFAULT_SAMPLE_RATE = 16_000


@dataclass(frozen=True, slots=True)
class ParakeetModelFiles:
    directory: Path
    encoder: Path
    decoder: Path
    joiner: Path
    tokens: Path


def discover_parakeet_model(explicit_directory: str | Path | None = None) -> ParakeetModelFiles:
    candidates: list[Path] = []
    if explicit_directory:
        candidates.append(Path(explicit_directory).expanduser())
    if configured := os.environ.get("INTERFAYCE_PARAKEET_MODEL"):
        candidates.append(Path(configured).expanduser())
    if appdata := os.environ.get("APPDATA"):
        plugins = Path(appdata) / "com.covas-next.ui" / "plugins"
        candidates.extend(sorted(plugins.glob("cn-plugin-parakett-stt*/model")))

    for directory in candidates:
        if not directory.is_dir():
            continue
        encoder = next(directory.glob("*encoder*.onnx"), None)
        decoder = next(directory.glob("*decoder*.onnx"), None)
        joiner = next(directory.glob("*joiner*.onnx"), None)
        tokens = next(directory.glob("*tokens*.txt"), None)
        if encoder and decoder and joiner and tokens:
            return ParakeetModelFiles(directory, encoder, decoder, joiner, tokens)
    raise FileNotFoundError(
        "Parakeet model not found. Set INTERFAYCE_PARAKEET_MODEL to its model directory."
    )


class ParakeetTranscriber:
    def __init__(self, model_directory: str | Path | None = None, threads: int | None = None):
        self.files = discover_parakeet_model(model_directory)
        self.threads = max(1, threads or max(1, (os.cpu_count() or 2) // 2))
        self.feature_dim = int(os.environ.get("INTERFAYCE_PARAKEET_FEATURE_DIM", "128"))
        self._recognizer: Any = None
        self._recognizer_lock = threading.Lock()

    def warm(self) -> None:
        self._get_recognizer()

    def _get_recognizer(self):
        with self._recognizer_lock:
            if self._recognizer is None:
                import sherpa_onnx  # type: ignore[import-not-found]

                self._recognizer = sherpa_onnx.OfflineRecognizer.from_transducer(
                    encoder=str(self.files.encoder),
                    decoder=str(self.files.decoder),
                    joiner=str(self.files.joiner),
                    tokens=str(self.files.tokens),
                    num_threads=self.threads,
                    feature_dim=self.feature_dim,
                    model_type="nemo_transducer",
                    debug=False,
                )
        return self._recognizer

    def transcribe(self, audio: object) -> str:
        """Transcribe a SpeechRecognition AudioData-compatible object."""

        import numpy as np  # type: ignore[import-not-found]

        raw_data = audio.get_raw_data(convert_rate=DEFAULT_SAMPLE_RATE, convert_width=2)
        samples = np.frombuffer(raw_data, dtype=np.int16).astype(np.float32) / 32768.0
        recognizer = self._get_recognizer()
        stream = recognizer.create_stream()
        stream.accept_waveform(DEFAULT_SAMPLE_RATE, samples)
        recognizer.decode_stream(stream)
        return stream.result.text.strip()


def capture_microphone_once(
    *,
    timeout_seconds: float = 5.0,
    phrase_seconds: float = 8.0,
    ambient_seconds: float = 0.25,
):
    """Capture one bounded utterance from the configured Windows default microphone."""

    import speech_recognition as sr  # type: ignore[import-not-found]

    recognizer = sr.Recognizer()
    configured = os.environ.get("INTERFAYCE_MICROPHONE", "").strip()
    device_index: int | None = None
    if configured:
        if configured.isdigit():
            device_index = int(configured)
        else:
            wanted = configured.casefold()
            device_index = next((index for index, name in enumerate(
                sr.Microphone.list_microphone_names()) if wanted in name.casefold()), None)
            if device_index is None:
                raise ValueError(f"Configured microphone was not found: {configured}")
    # Use the device's native sample rate while recording. AudioData resamples to
    # Parakeet's required 16 kHz in transcribe(); Beyond reports 44.1/48 kHz and
    # rejects a forced 16 kHz stream.
    with sr.Microphone(device_index=device_index, sample_rate=None) as source:
        recognizer.adjust_for_ambient_noise(source, duration=ambient_seconds)
        return recognizer.listen(
            source,
            timeout=timeout_seconds,
            phrase_time_limit=phrase_seconds,
        )
