import pytest
pytest.importorskip("flask")
from pathlib import Path
from unittest.mock import patch
from stt_server.app import create_app
from stt_server.backends import MoonshineBackend


def test_stream_auth_order_bounds_and_final_cleanup():
    class Stream:
        closed = 0
        def __init__(self, backend):
            pass
        def push(self, pcm, final=False):
            return "hello world" if final else "hello"
        def close(self):
            Stream.closed += 1
    app = create_app({"api_key": "test", "default_model": "moonshine",
        "models": {"moonshine": {"backend": "moonshine"}}}, Path("."))
    registry = app.extensions["stt_registry"]
    headers = {"Authorization": "Bearer test"}
    with patch.object(registry, "get", return_value=MoonshineBackend({}, Path("."))), \
         patch("stt_server.streaming.MoonshineStream", Stream):
        client = app.test_client()
        assert client.post("/v1/audio/streams").status_code == 401
        key = client.post("/v1/audio/streams", headers=headers).json["session"]
        url = "/v1/audio/streams/" + key
        assert client.post(url+"?sequence=1", headers=headers, data=b"00").status_code == 409
        assert client.post(url+"?sequence=0", headers=headers, data=b"0").status_code == 400
        assert client.post(url+"?sequence=0", headers=headers, data=bytes(160002)).status_code == 400
        assert client.post(url+"?sequence=0", headers=headers, data=b"00").json["text"] == "hello"
        assert client.post(url+"?sequence=1&final=1", headers=headers).json["text"] == "hello world"
        assert client.delete(url, headers=headers).status_code == 404
        assert Stream.closed == 1
