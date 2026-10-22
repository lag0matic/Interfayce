from types import SimpleNamespace
from unittest.mock import Mock
import threading
import pytest
pytest.importorskip("numpy")
from stt_server.whisper_stream import WhisperStream


def backend():
    model = Mock()
    model.transcribe.side_effect = lambda audio, **kw: (iter([SimpleNamespace(text=" Hello, world?")]), None)
    return SimpleNamespace(_model=model, _lock=threading.Lock(), options={})


def test_buffers_context_throttles_and_flushes_short_final():
    b = backend()
    s = WhisperStream(b)
    assert s.push(bytes(16000)) == ""
    b._model.transcribe.assert_not_called()
    assert s.push(bytes(16000)) == "Hello, world?"
    assert len(b._model.transcribe.call_args.args[0]) == 16000
    s.push(bytes(8000))
    assert b._model.transcribe.call_count == 1
    assert s.push(b"", final=True) == "Hello, world?"
    assert len(b._model.transcribe.call_args.args[0]) == 20000
    with pytest.raises(RuntimeError):
        s.push(b"")
    s.close()
    s.close()
    assert not s.pcm


def test_empty_final_invalid_audio_limit_and_independent_sessions():
    b = backend()
    a, c = WhisperStream(b), WhisperStream(b)
    with pytest.raises(ValueError):
        a.push(b"x")
    with pytest.raises(ValueError):
        a.push(bytes(32 * 32000))
    assert not a.pcm
    a.push(bytes(16000), final=True)
    assert c.push(b"", final=True) == ""
    assert b._model.transcribe.call_count == 1
