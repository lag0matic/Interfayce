import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.remote_stt import (
    RemoteSttTranscriber, transcribe_audio_file, transcription_endpoint,
    valid_remote_stt_endpoint,
)


class _Audio:
    def get_wav_data(self, **kwargs):
        return b"RIFF-test-wave"


class _Fallback:
    def __init__(self): self.called = False
    def warm(self): self.called = True
    def transcribe(self, audio): self.called = True; return "local result"


class _Response:
    status = 200
    def __init__(self, payload): self.payload = payload
    def __enter__(self): return self
    def __exit__(self, *args): return False
    def read(self, *args): return json.dumps(self.payload).encode()


class RemoteSttTests(unittest.TestCase):
    def test_endpoint_normalization(self):
        self.assertTrue(valid_remote_stt_endpoint("http://server:5010"))
        self.assertFalse(valid_remote_stt_endpoint("server:5010"))
        self.assertEqual(transcription_endpoint("http://server:5010/"),
                         "http://server:5010/v1/audio/transcriptions")

    @patch("interfayce.remote_stt.load_remote_stt_api_key", return_value="key")
    @patch("interfayce.remote_stt.urlopen")
    def test_remote_transcription(self, open_url, _load_key):
        open_url.return_value = _Response({"text": "remote result"})
        transcriber = RemoteSttTranscriber("http://server:5010", "whisper-turbo")
        self.assertEqual(transcriber.transcribe(_Audio()), "remote result")
        request = open_url.call_args.args[0]
        self.assertIn(b'name="model"', request.data)
        self.assertIn(b"whisper-turbo", request.data)

    @patch("interfayce.remote_stt.load_remote_stt_api_key", return_value="key")
    @patch("interfayce.remote_stt.urlopen")
    def test_remote_warmup_runs_silent_audio_through_model(self, open_url, _load_key):
        open_url.return_value = _Response({"text": ""})
        RemoteSttTranscriber("http://server:5010", "whisper-turbo").warm()
        request = open_url.call_args.args[0]
        self.assertEqual(request.full_url,
                         "http://server:5010/v1/audio/transcriptions")
        self.assertIn(b"RIFF", request.data)
        self.assertIn(b"whisper-turbo", request.data)

    @patch("interfayce.remote_stt.load_remote_stt_api_key", return_value="key")
    @patch("interfayce.remote_stt.urlopen", side_effect=OSError("offline"))
    def test_failure_uses_local_fallback(self, _open_url, _load_key):
        fallback = _Fallback()
        transcriber = RemoteSttTranscriber("http://server:5010", "moonshine", fallback)
        self.assertEqual(transcriber.transcribe(_Audio()), "local result")
        self.assertTrue(fallback.called)

    @patch("interfayce.remote_stt.load_remote_stt_api_key", return_value="key")
    @patch("interfayce.remote_stt.urlopen")
    def test_audio_file_is_sent_directly_without_conversion(self, open_url, _load_key):
        open_url.return_value = _Response({"text": "play some Bowie"})
        with TemporaryDirectory() as directory:
            path = Path(directory) / "voice sample.m4a"
            path.write_bytes(b"m4a-audio-data")
            transcript = transcribe_audio_file(
                path, "http://server:5010", "whisper-turbo")
        self.assertEqual(transcript, "play some Bowie")
        request = open_url.call_args.args[0]
        self.assertIn(b"m4a-audio-data", request.data)
        self.assertIn(b'filename="voice_sample.m4a"', request.data)
        self.assertEqual(request.headers["Authorization"], "Bearer key")


if __name__ == "__main__":
    unittest.main()
