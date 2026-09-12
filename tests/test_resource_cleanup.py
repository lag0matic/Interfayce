"""Failure-path resource tests; no physical audio or live service required."""

from http.server import BaseHTTPRequestHandler
from io import BytesIO
import socket
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch
import wave

from interfayce import capture_cue, kokoro
from interfayce.voice_service import BoundedVoiceServer


def silent_wav():
    buffer = BytesIO()
    with wave.open(buffer, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(16000)
        output.writeframes(b"\0\0" * 32)
    return buffer.getvalue()


class AudioCleanupTests(unittest.TestCase):
    def exercise(self, cue, failure):
        audio = MagicMock()
        stream = audio.open.return_value
        if failure == "open":
            audio.open.side_effect = OSError("device disappeared")
        elif failure == "stop":
            stream.stop_stream.side_effect = OSError("device disappeared")
        elif failure == "write":
            stream.write.side_effect = OSError("device disappeared")
        elif failure == "default":
            audio.get_default_output_device_info.side_effect = OSError("no default")
        module = capture_cue if cue else kokoro
        with patch.dict("sys.modules", {"pyaudio": SimpleNamespace(
                PyAudio=lambda: audio, paInt16=8)}), \
                patch.object(module, "configured_output_device_index",
                    side_effect=OSError("lookup failed") if failure == "lookup" else None,
                    return_value=None), \
                patch.object(kokoro, "load_settings", return_value=SimpleNamespace(
                    tts_muted=False, tts_volume=1.0)), \
                patch.dict("os.environ", {"INTERFAYCE_CAPTURE_CUES": "on"}):
            if cue:
                with patch.object(capture_cue.LOGGER, "warning"):
                    capture_cue.play_capture_cue(True)
            elif failure:
                with self.assertRaises(OSError):
                    kokoro.play_wav(silent_wav())
            else:
                kokoro.play_wav(silent_wav())
        audio.terminate.assert_called_once()
        if failure not in {"lookup", "default", "open"}:
            stream.close.assert_called_once()

    def test_tts_cleanup_on_success_and_device_failures(self):
        for failure in (None, "lookup", "default", "open", "write", "stop"):
            with self.subTest(failure=failure):
                self.exercise(False, failure)

    def test_cue_cleanup_on_success_and_device_failures(self):
        for failure in (None, "lookup", "open", "write", "stop"):
            with self.subTest(failure=failure):
                self.exercise(True, failure)


class VoiceConnectionTests(unittest.TestCase):
    def test_stalled_headers_expire_and_release_connection_slot(self):
        entered = threading.Event()
        completed = threading.Event()

        class Handler(BaseHTTPRequestHandler):
            def handle(self):
                entered.set()
                try:
                    super().handle()
                finally:
                    completed.set()

            def log_message(self, *args):
                pass

        class Server(BoundedVoiceServer):
            connection_timeout = 0.1
            max_connections = 1

            def process_request_thread(self, *args):
                try:
                    super().process_request_thread(*args)
                finally:
                    released.set()

        released = threading.Event()
        with Server(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.handle_request)
            thread.start()
            with socket.create_connection(server.server_address, timeout=2) as client:
                client.sendall(b"GET /health HTTP/1.0\r\nX-Incomplete:")
                self.assertTrue(entered.wait(2))
                self.assertTrue(completed.wait(2))
                self.assertTrue(released.wait(2))
            thread.join(2)
            self.assertFalse(thread.is_alive())
            self.assertTrue(server._connection_slots.acquire(blocking=False))
            server._connection_slots.release()

    def test_over_capacity_connections_are_closed_without_worker(self):
        with BoundedVoiceServer(("127.0.0.1", 0), BaseHTTPRequestHandler) as server:
            for _ in range(server.max_connections):
                self.assertTrue(server._connection_slots.acquire(blocking=False))
            request = MagicMock()
            with patch.object(server, "shutdown_request") as close, \
                    patch("http.server.ThreadingHTTPServer.process_request") as spawn:
                server.process_request(request, ("127.0.0.1", 1))
                close.assert_called_once_with(request)
                spawn.assert_not_called()

    def test_worker_start_failure_releases_slot(self):
        with BoundedVoiceServer(("127.0.0.1", 0), BaseHTTPRequestHandler) as server:
            with patch("http.server.ThreadingHTTPServer.process_request",
                    side_effect=RuntimeError("thread creation failed")):
                with self.assertRaises(RuntimeError):
                    server.process_request(MagicMock(), ("127.0.0.1", 1))
            for _ in range(server.max_connections):
                self.assertTrue(server._connection_slots.acquire(blocking=False))
