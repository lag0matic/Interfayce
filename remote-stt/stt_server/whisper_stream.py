"""Buffered Whisper for bounded push-to-talk: retain context, revise partials."""
import threading


class WhisperStream:
    def __init__(self, backend):
        self.backend = backend
        self.pcm = bytearray()
        self.decoded_bytes = 0
        self.text = ""
        self.finished = False
        self.lock = threading.RLock()
        # Decode at most once per second of NEW audio, not every HTTP packet.
        self.interval = 32000

    def push(self, pcm, final=False):
        import numpy as np
        with self.lock:
            if self.finished:
                raise RuntimeError("Stream already finished or closed.")
            if len(pcm) % 2 or len(self.pcm) + len(pcm) > 31 * 32000:
                raise ValueError("Expected at most 31 seconds of mono PCM16.")
            self.pcm.extend(pcm)
            if not final and len(self.pcm) - self.decoded_bytes < self.interval:
                return self.text
            if final:
                self.finished = True
            if not self.pcm:
                return self.text
            # All streams are bounded to 31 s. Re-decoding the entire utterance
            # avoids dropped/duplicated words at sliding-window seams and lets
            # sentence endings correct earlier punctuation.
            audio = np.frombuffer(self.pcm, dtype="<i2").astype(np.float32) / 32768.0
            options = self.backend.options
            with self.backend._lock:
                segments, _ = self.backend._model.transcribe(
                    audio, language=options.get("language", "en"),
                    beam_size=int(options.get("beam_size", 5)),
                    temperature=0.0, condition_on_previous_text=False,
                    vad_filter=bool(options.get("vad_filter", True)))
                self.text = " ".join(segment.text.strip() for segment in segments).strip()
            self.decoded_bytes = len(self.pcm)
            return self.text

    def close(self):
        with self.lock:
            self.finished = True
            self.pcm.clear()
