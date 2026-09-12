"""Lazy, reusable transcription backends."""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
import threading
from typing import Any


class Backend(ABC):
    def __init__(self, options: dict[str, Any], model_root: Path):
        self.options = options
        self.model_root = model_root

    @abstractmethod
    def warm(self) -> None:
        """Load the model and download any missing assets."""

    @abstractmethod
    def transcribe(self, audio_path: Path, language: str | None = None) -> str:
        """Transcribe an audio file."""


class FasterWhisperBackend(Backend):
    def __init__(self, options: dict[str, Any], model_root: Path):
        super().__init__(options, model_root)
        self._model: Any = None
        self._lock = threading.Lock()

    def warm(self) -> None:
        with self._lock:
            if self._model is not None:
                return
            from faster_whisper import WhisperModel
            import numpy as np

            self.model_root.mkdir(parents=True, exist_ok=True)
            model = WhisperModel(
                self.options.get("model", "turbo"),
                device=self.options.get("device", "cuda"),
                compute_type=self.options.get("compute_type", "int8_float16"),
                download_root=str(self.model_root / "faster-whisper"),
            )
            # Loading weights alone does not initialize CUDA kernels or prove
            # that cuBLAS/cuDNN can be loaded. Force one discarded decode so
            # health only reports the model ready after the complete inference
            # path works, and the first real voice command stays warm.
            segments, _ = model.transcribe(
                np.zeros(16_000, dtype=np.float32),
                language=self.options.get("language", "en"),
                beam_size=1,
                condition_on_previous_text=False,
                vad_filter=False,
            )
            list(segments)
            self._model = model

    def transcribe(self, audio_path: Path, language: str | None = None) -> str:
        self.warm()
        with self._lock:
            segments, _ = self._model.transcribe(
                str(audio_path),
                language=language or self.options.get("language", "en"),
                beam_size=int(self.options.get("beam_size", 5)),
                condition_on_previous_text=False,
                vad_filter=bool(self.options.get("vad_filter", True)),
            )
            return " ".join(segment.text.strip() for segment in segments).strip()


class MoonshineBackend(Backend):
    def __init__(self, options: dict[str, Any], model_root: Path):
        super().__init__(options, model_root)
        self._model: Any = None
        self._lock = threading.Lock()

    def warm(self) -> None:
        with self._lock:
            if self._model is not None:
                return
            from moonshine_voice import Transcriber, get_model_for_language

            self.model_root.mkdir(parents=True, exist_ok=True)
            model_arch = self.options.get("model_arch")
            model_path, resolved_arch = get_model_for_language(
                self.options.get("language", "en"),
                model_arch,
                cache_root=self.model_root / "moonshine",
            )
            self._model = Transcriber(model_path=model_path, model_arch=resolved_arch)

    def transcribe(self, audio_path: Path, language: str | None = None) -> str:
        configured = self.options.get("language", "en")
        if language and language != configured:
            raise ValueError(
                f"Moonshine model is configured for {configured!r}, not {language!r}."
            )
        self.warm()
        from moonshine_voice import load_wav_file

        audio_data, sample_rate = load_wav_file(str(audio_path))
        with self._lock:
            transcript = self._model.transcribe_without_streaming(
                audio_data, sample_rate=sample_rate, flags=0
            )
        return " ".join(
            line.text.strip() for line in transcript.lines if line.text.strip()
        ).strip()



class MoonshineStream:
    """One bounded utterance; share model weights, keep per-stream state."""
    def __init__(self, backend):
        self.backend = backend
        self.lines = {}
        with backend._lock:
            self.stream = backend._model.create_stream(update_interval=60.0)
            try:
                self.stream.start()
            except Exception:
                self.stream.close()
                raise

    def push(self, pcm: bytes, final=False):
        import numpy as np
        with self.backend._lock:
            if pcm:
                samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
                self.stream.add_audio(samples.tolist(), sample_rate=16000)
            transcript = self.stream.stop() if final else self.stream.update_transcription()
            if transcript is None:
                raise RuntimeError("Streaming recognizer did not return a transcript.")
            for line in transcript.lines:
                self.lines[line.line_id] = line
            return " ".join(line.text.strip() for line in sorted(
                self.lines.values(), key=lambda line: line.start_time) if line.text.strip())

    def close(self):
        with self.backend._lock:
            self.stream.close()


def create_backend(options: dict[str, Any], model_root: Path) -> Backend:
    backend_name = options.get("backend")
    if backend_name == "faster-whisper":
        return FasterWhisperBackend(options, model_root)
    if backend_name == "moonshine":
        return MoonshineBackend(options, model_root)
    raise ValueError(f"Unsupported STT backend: {backend_name!r}")
