from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import types
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from stt_server.backends import FasterWhisperBackend, MoonshineBackend, create_backend


class _Segment:
    def __init__(self, text: str):
        self.text = text


class BackendTests(unittest.TestCase):
    def test_create_backend_rejects_unknown_kind(self):
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            create_backend({"backend": "mystery"}, Path("models"))

    def test_faster_whisper_model_is_reused_and_segments_joined(self):
        instances = []

        class FakeModel:
            def __init__(self, *args, **kwargs):
                instances.append((args, kwargs))

            def transcribe(self, path, **kwargs):
                return iter((_Segment(" hello "), _Segment("world"))), object()

        fake = types.ModuleType("faster_whisper")
        fake.WhisperModel = FakeModel
        original = sys.modules.get("faster_whisper")
        sys.modules["faster_whisper"] = fake
        try:
            with tempfile.TemporaryDirectory() as directory:
                backend = FasterWhisperBackend(
                    {"model": "turbo", "device": "cuda", "compute_type": "int8_float16"},
                    Path(directory),
                )
                self.assertEqual(backend.transcribe(Path("sample.wav")), "hello world")
                self.assertEqual(backend.transcribe(Path("sample.wav")), "hello world")
                self.assertEqual(len(instances), 1)
        finally:
            if original is None:
                sys.modules.pop("faster_whisper", None)
            else:
                sys.modules["faster_whisper"] = original

    def test_moonshine_lines_are_joined(self):
        class Line:
            def __init__(self, text): self.text = text

        class FakeTranscriber:
            def __init__(self, **kwargs): pass
            def transcribe_without_streaming(self, data, sample_rate, flags):
                return types.SimpleNamespace(lines=[Line(" one "), Line(""), Line("two")])

        fake = types.ModuleType("moonshine_voice")
        fake.Transcriber = FakeTranscriber
        fake.get_model_for_language = lambda *args, **kwargs: ("model", 1)
        fake.load_wav_file = lambda path: ([0.0], 16000)
        original = sys.modules.get("moonshine_voice")
        sys.modules["moonshine_voice"] = fake
        try:
            with tempfile.TemporaryDirectory() as directory:
                backend = MoonshineBackend({"language": "en"}, Path(directory))
                self.assertEqual(backend.transcribe(Path("sample.wav")), "one two")
        finally:
            if original is None:
                sys.modules.pop("moonshine_voice", None)
            else:
                sys.modules["moonshine_voice"] = original


if __name__ == "__main__":
    unittest.main()
