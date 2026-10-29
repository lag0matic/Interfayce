import threading
from types import SimpleNamespace
from unittest.mock import Mock

from interfayce.voice_service import VoiceRuntime
from interfayce.music_voice_capture import MusicVoiceCapture
from interfayce import voice_service


def runtime_for_test(monkeypatch):
    runtime = object.__new__(VoiceRuntime)
    runtime.command_lock = threading.Lock()
    runtime._assistant_lock = threading.Lock()
    runtime._answer_capture_lock = threading.Lock()
    runtime._assistant_speech_cancel = threading.Event()
    runtime._assistant_status = 'READY'
    runtime._assistant_transcript = runtime._assistant_response = ''
    runtime.assistant_session = SimpleNamespace(generation=0, backend='codex', codex=Mock())
    def cancel():
        runtime.assistant_session.generation += 1
    runtime.assistant_session.cancel = cancel
    runtime.assistant_session.codex.ask.return_value = 'Answer'
    runtime.transcriber = Mock()
    runtime.transcriber.transcribe.return_value = 'A longer question with pauses'
    runtime.assistant_capture = MusicVoiceCapture(runtime.assistant_command, label='Assistant', result_timeout=660)
    monkeypatch.setattr(voice_service, 'speak_in_background', Mock())
    # Keep capture semantics but remove audio-device cues from these tests.
    monkeypatch.setattr(voice_service, 'capture_with_cues',
                        lambda capture, *args, cue=None, **kwargs: capture(*args, **kwargs))
    return runtime


def test_ask_waits_for_release_then_transcribes_and_submits(monkeypatch):
    runtime = runtime_for_test(monkeypatch)
    entered = threading.Event()
    def capture(stop_event, *, max_seconds):
        assert max_seconds == 30.0
        entered.set()
        assert stop_event.wait(2)
        return 'held audio'
    monkeypatch.setattr(voice_service, 'capture_microphone_until', capture)
    token = runtime.assistant_capture.start()
    assert entered.wait(1)
    runtime.transcriber.transcribe.assert_not_called()
    runtime.assistant_session.codex.ask.assert_not_called()
    assert runtime.assistant_capture.stop(token)
    runtime.assistant_capture.result(token)
    runtime.transcriber.transcribe.assert_called_once_with('held audio')
    assert runtime.assistant_session.codex.ask.call_args.args[0] == 'A longer question with pauses'
    assert not runtime.command_lock.locked()


def test_cancel_releases_microphone_without_sending_partial_question(monkeypatch):
    runtime = runtime_for_test(monkeypatch)
    entered = threading.Event()
    def capture(stop_event, **kwargs):
        entered.set()
        assert stop_event.wait(2)
        return 'partial audio'
    monkeypatch.setattr(voice_service, 'capture_microphone_until', capture)
    token = runtime.assistant_capture.start()
    assert entered.wait(1)
    runtime.cancel_assistant()
    assert runtime.assistant_capture.result(token).startswith('CANCELLED')
    runtime.transcriber.transcribe.assert_not_called()
    runtime.assistant_session.codex.ask.assert_not_called()
    assert not runtime.command_lock.locked()


def test_empty_recording_does_not_send_an_llm_request(monkeypatch):
    runtime = runtime_for_test(monkeypatch)
    runtime.transcriber.transcribe.return_value = ''
    monkeypatch.setattr(voice_service, 'capture_microphone_until', lambda *a, **k: 'silence')
    token = runtime.assistant_capture.start()
    assert runtime.assistant_capture.result(token).startswith('READY')
    runtime.assistant_session.codex.ask.assert_not_called()


def test_cancel_before_capture_worker_starts_is_not_lost(monkeypatch):
    runtime = runtime_for_test(monkeypatch)
    runtime.assistant_session.generation = 1
    capture = Mock()
    monkeypatch.setattr(voice_service, 'capture_microphone_until', capture)
    token = runtime.assistant_capture.start(expected_generation=0)
    assert runtime.assistant_capture.result(token).startswith('CANCELLED')
    capture.assert_not_called()
    runtime.assistant_session.codex.ask.assert_not_called()
    assert not runtime.command_lock.locked()
