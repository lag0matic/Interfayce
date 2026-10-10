from pathlib import Path
from unittest.mock import Mock
import threading

import pytest
pytest.importorskip("numpy")
pytest.importorskip("flask")
from stt_server.nemotron import NemotronStream, NemotronBackend
from stt_server.backends import create_backend
from stt_server.app import create_app
from stt_server.app import ModelRegistry
from unittest.mock import patch


def test_health_only_ready_after_model_warmup_finishes():
    registry = ModelRegistry({"nemotron": {"backend": "nemotron"}}, Path("."))
    backend = Mock()
    def warming():
        assert not registry.status()["nemotron"]["loaded"]
    backend.warm.side_effect = warming
    with patch("stt_server.app.create_backend", return_value=backend):
        registry.get("nemotron")
    assert registry.status()["nemotron"]["loaded"]


def test_chunks_keep_state_and_final_flushes_once():
    model = Mock()
    model.is_ready.side_effect = [True, False, False]
    model.get_result.side_effect = ["hello", "hello world"]
    backend = Mock(_lock=threading.RLock())
    stream = NemotronStream(backend, model)
    native = model.create_stream.return_value
    assert stream.push(b"\xff\x7f\x00\x80") == "hello"
    samples = native.accept_waveform.call_args.args[1]
    assert samples[0] == pytest.approx(32767 / 32768)
    assert samples[1] == -1.0
    assert stream.push(b"", final=True) == "hello world"
    assert len(native.accept_waveform.call_args.args[1]) == 10560
    native.input_finished.assert_called_once()
    model.create_stream.assert_called_once()
    with pytest.raises(RuntimeError):
        stream.push(b"", final=True)
    stream.close()
    stream.close()
    assert stream.stream is None


def test_factory_and_language_validation(tmp_path):
    backend = create_backend({"backend": "nemotron"}, tmp_path)
    assert isinstance(backend, NemotronBackend)
    with pytest.raises(ValueError):
        backend.transcribe(tmp_path / "missing.wav", language="fr")


def test_streaming_accepts_backend_interface_and_reports_selection():
    app = create_app({"api_key": "test", "default_model": "moonshine",
        "streaming_model": "nemotron", "models": {
            "moonshine": {"backend": "moonshine"},
            "nemotron": {"backend": "nemotron"}}}, Path("."))
    registry = app.extensions["stt_registry"]
    backend = Mock()
    backend.create_stream.return_value.push.return_value = "still live"
    registry.get = Mock(return_value=backend)
    headers = {"Authorization": "Bearer test"}
    client = app.test_client()
    assert client.get("/health", headers=headers).json["streaming_model"] == "nemotron"
    response = client.post("/v1/audio/streams", headers=headers)
    assert response.json["model"] == "nemotron"
    registry.get.assert_called_once_with("nemotron")
    url = "/v1/audio/streams/" + response.json["session"]
    assert client.post(url + "?sequence=0&final=1", data=b"00", headers=headers).json["text"] == "still live"
    backend.create_stream.return_value.close.assert_called_once()


def test_nonstreaming_backend_fails_without_creating_session():
    app = create_app({"api_key": "test", "default_model": "whisper",
        "streaming_model": "whisper", "models": {"whisper": {"backend": "faster-whisper"}}}, Path("."))
    backend = Mock()
    backend.create_stream.side_effect = NotImplementedError()
    app.extensions["stt_registry"].get = Mock(return_value=backend)
    assert app.test_client().post("/v1/audio/streams", headers={"Authorization": "Bearer test"}).status_code == 503
