from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.assistant import AssistantResult, AssistantState
from interfayce.assistant_harness import AssistantHarness, tts_text
from interfayce.settings import AppSettings


class _Assistant:
    def ask(self, transcript):
        return AssistantResult(
            True,
            "An **EFHW** is resonant [S1].",
            AssistantState.RESPONDING,
            ("search_web", "open_search_result"),
        )


class _Research:
    def citations(self):
        return {
            "[S1]": {"title": "Antenna Guide", "url": "https://example.com/guide"},
            "[S2]": {"title": "Unused", "url": "https://example.com/unused"},
        }


class AssistantHarnessTests(unittest.TestCase):
    def test_display_answer_keeps_citations_but_speech_does_not(self):
        harness = AssistantHarness(assistant=_Assistant(), research=_Research())
        result = harness.ask("Tell me about EFHW antennas")
        self.assertIn("[S1]", result.answer)
        self.assertEqual(result.spoken_answer, "An EFHW is resonant.")
        self.assertEqual(result.sources, ((
            "[S1]", "Antenna Guide", "https://example.com/guide"
        ),))

    def test_synchronous_speech_uses_clean_answer(self):
        harness = AssistantHarness(assistant=_Assistant(), research=_Research())
        with patch("interfayce.kokoro.synthesize", return_value=b"wav") as synthesize, \
                patch("interfayce.kokoro.play_wav") as play:
            harness.ask("question", speak=True)
        synthesize.assert_called_once_with("An EFHW is resonant.")
        play.assert_called_once_with(b"wav")

    def test_audio_file_uses_configured_remote_stt_then_assistant(self):
        harness = AssistantHarness(assistant=_Assistant())
        with TemporaryDirectory() as directory:
            path = Path(directory) / "sample.m4a"
            path.write_bytes(b"audio")
            with patch("interfayce.assistant_harness.load_settings", return_value=AppSettings(
                    stt_endpoint="http://server:5010", stt_model="whisper-turbo")), \
                    patch("interfayce.assistant_harness.transcribe_audio_file",
                          return_value="What is an EFHW?") as transcribe:
                result = harness.ask_audio_file(path)
        transcribe.assert_called_once_with(
            path, "http://server:5010", "whisper-turbo")
        self.assertEqual(result.transcript, "What is an EFHW?")

    def test_tts_cleanup_is_bounded(self):
        self.assertEqual(tts_text("**Answer** [S1]"), "Answer")
        self.assertLessEqual(len(tts_text("x" * 2_000)), 1_200)


if __name__ == "__main__":
    unittest.main()
