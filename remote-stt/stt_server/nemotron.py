"""Native streaming Nemotron through sherpa-onnx; no whole-recording re-decode."""
from pathlib import Path
import threading
import wave

from .backends import Backend

MODEL_DIRECTORY = "nemotron-en-560ms"


class NemotronBackend(Backend):
    def __init__(self, options, model_root):
        super().__init__(options, model_root)
        self._lock = threading.RLock()
        self._model = None

    def warm(self):
        with self._lock:
            if self._model is not None:
                return
            import sherpa_onnx
            folder = self.model_root / self.options.get("model_directory", MODEL_DIRECTORY)
            files = {name: folder / filename for name, filename in {
                "encoder": "encoder.int8.onnx", "decoder": "decoder.int8.onnx",
                "joiner": "joiner.int8.onnx", "tokens": "tokens.txt"}.items()}
            for path in files.values():
                if not path.is_file():
                    raise FileNotFoundError(f"Missing {path}; run prepare-nemotron.py")
            model = sherpa_onnx.OnlineRecognizer.from_transducer(
                **{name: str(path) for name, path in files.items()},
                sample_rate=16000, feature_dim=128,
                num_threads=int(self.options.get("num_threads", 4)),
                provider=self.options.get("provider", "cpu"),
                decoding_method="greedy_search", enable_endpoint_detection=False)
            # Readiness includes a complete decode, not just loading weights.
            stream = NemotronStream(self, model)
            try:
                stream.push(bytes(16000), final=True)
            finally:
                stream.close()
            self._model = model

    def create_stream(self):
        self.warm()
        return NemotronStream(self, self._model)

    def transcribe(self, audio_path: Path, language=None):
        if language and language != "en":
            raise ValueError("This Nemotron model supports English only.")
        with wave.open(str(audio_path), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 16000):
                raise ValueError("Nemotron file input requires mono PCM16 WAV at 16 kHz.")
            pcm = audio.readframes(audio.getnframes())
        stream = self.create_stream()
        try:
            return stream.push(pcm, final=True)
        finally:
            stream.close()


class NemotronStream:
    def __init__(self, backend, model):
        self.backend = backend
        self.model = model
        with backend._lock:
            self.stream = model.create_stream()
        self.finished = False

    def push(self, pcm, final=False):
        import numpy as np
        with self.backend._lock:
            if self.stream is None or self.finished:
                raise RuntimeError("Stream already finished or closed.")
            if len(pcm) % 2:
                raise ValueError("Expected PCM16 samples.")
            if pcm:
                samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
                self.stream.accept_waveform(16000, samples)
            if final:
                # Upstream's 660 ms tail flushes the encoder's remaining context.
                self.stream.accept_waveform(16000, np.zeros(10560, dtype=np.float32))
                self.stream.input_finished()
                self.finished = True
            while self.model.is_ready(self.stream):
                self.model.decode_stream(self.stream)
            return self.model.get_result(self.stream).strip()

    def close(self):
        with self.backend._lock:
            self.stream = None
            self.model = None
            self.finished = True
