import json
import queue
from types import SimpleNamespace

import pytest

from interfayce.codex_backend import CodexBackend
from interfayce.codex_transport import CodexConnectionError


class Transport:
    def __init__(self, cwd):
        self.events = queue.Queue()
        self.process = SimpleNamespace(poll=lambda: None)
        self.calls = []
        self.closed = False

    def request(self, method, params):
        self.calls.append((method, params))
        if method == 'account/read':
            return {'account': {'type': 'chatgpt'}}
        if method in ('thread/start', 'thread/resume'):
            return {'thread': {'id': 'thread-test'}}
        if method == 'turn/start':
            for delta in ('Hello ', 'VR'):
                self.events.put({'method': 'item/agentMessage/delta', 'params': {
                    'threadId': 'thread-test', 'turnId': 'turn-test', 'itemId': 'message-test', 'delta': delta}})
            self.events.put({'method': 'turn/completed', 'params': {
                'threadId': 'thread-test', 'turn': {'id': 'turn-test', 'status': 'completed'}}})
            return {'turn': {'id': 'turn-test'}}
        return {}

    def close(self):
        self.closed = True


def test_stream_and_resume_reference(tmp_path):
    first = CodexBackend(tmp_path, Transport)
    assert first.ask('Hi') == 'Hello VR'
    assert json.loads((tmp_path / 'conversation.json').read_text())['threadId'] == 'thread-test'
    first.close()
    second = CodexBackend(tmp_path, Transport)
    assert second.ask('Again') == 'Hello VR'
    assert 'thread/resume' in [m for m, _ in second._transport.calls]
    second.new_conversation()
    assert second.thread_id is None
    assert not (tmp_path / 'conversation.json').exists()


def test_no_api_key_fallback(tmp_path):
    class ApiTransport(Transport):
        def request(self, method, params):
            return {'account': {'type': 'apiKey'}}
    backend = CodexBackend(tmp_path, ApiTransport)
    with pytest.raises(CodexConnectionError, match='Sign in'):
        backend.ask('Hi')
    assert not backend.snapshot()['active']


def test_cancel_never_speaks_completed_race(tmp_path):
    backend = CodexBackend(tmp_path, Transport)
    assert backend.ask('Hi', on_update=backend.cancel) == ''
    assert backend.state == 'CANCELLED'
    assert 'turn/interrupt' in [m for m, _ in backend._transport.calls]


def test_stale_approval_rejected(tmp_path):
    backend = CodexBackend(tmp_path, Transport)
    with pytest.raises(ValueError, match='no longer active'):
        backend.respond(123, {'decision': 'accept'})


def test_question_choices_are_correlated_and_can_be_dictated(tmp_path):
    backend = CodexBackend(tmp_path, Transport)
    sent = []
    backend._transport = SimpleNamespace(send=sent.append)
    backend.pending = {'id': 'request-1', 'method': 'item/tool/requestUserInput', 'params': {
        'questions': [{'id': 'first', 'question': 'Which?', 'options': [{'label': 'A'}]},
                      {'id': 'second', 'question': 'Any details?', 'options': []}]}}
    backend._question_card()
    token = backend.card['token']
    backend.choose(token, 0)
    with pytest.raises(ValueError):
        backend.choose(token, 0)
    backend.dictate_answer(backend.card['token'], 'Something quiet')
    assert sent == [{'id': 'request-1', 'result': {'answers': {
        'first': {'answers': ['A']}, 'second': {'answers': ['Something quiet']}}}}]
    assert backend.card is None


def test_disconnect_does_not_replay_turn(tmp_path):
    instances = []
    class Broken(Transport):
        def __init__(self, cwd):
            super().__init__(cwd)
            instances.append(self)
        def request(self, method, params):
            if method == 'turn/start':
                self.calls.append((method, params))
                raise CodexConnectionError('Disconnected')
            return super().request(method, params)
    backend = CodexBackend(tmp_path, Broken)
    with pytest.raises(CodexConnectionError):
        backend.ask('Do something')
    assert len(instances) == 1
    assert [m for m, _ in instances[0].calls].count('turn/start') == 1
    assert instances[0].closed


def test_model_choice_is_explicit_and_changes_on_next_turn(tmp_path, monkeypatch):
    from interfayce.settings import AppSettings, save_settings, load_settings
    monkeypatch.setenv('INTERFAYCE_SETTINGS_PATH', str(tmp_path / 'settings.json'))
    assert load_settings().codex_model == 'gpt-5.6-luna'
    backend = CodexBackend(tmp_path / 'conversation', Transport)
    backend.ask('Hi')
    first = [p for m, p in backend._transport.calls if m == 'turn/start'][-1]
    assert first['model'] == 'gpt-5.6-luna'
    assert first['serviceTier'] == 'default'
    save_settings(AppSettings(codex_model='gpt-5.6-terra', llm_model='music-model'))
    backend.ask('Again')
    second = [p for m, p in backend._transport.calls if m == 'turn/start'][-1]
    assert second['model'] == 'gpt-5.6-terra'
    assert load_settings().llm_model == 'music-model'


def test_cancelled_capture_is_not_submitted(tmp_path):
    import threading
    cancellation = threading.Event()
    cancellation.set()
    backend = CodexBackend(tmp_path, lambda _: pytest.fail('Must not connect'))
    assert backend.ask('Cancelled capture', cancel=cancellation) == ''
    assert backend.state == 'CANCELLED'


def test_idle_disconnect_reconnects_before_new_submission(tmp_path):
    backend = CodexBackend(tmp_path, Transport)
    backend.ask('First')
    old = backend._transport
    old.connected = False
    assert backend.ask('Second') == 'Hello VR'
    assert backend._transport is not old and old.closed
    assert [m for m, _ in backend._transport.calls].count('turn/start') == 1
    assert 'thread/resume' in [m for m, _ in backend._transport.calls]


def test_close_during_connection_cannot_publish_child(tmp_path):
    created = []
    def factory(directory):
        child = Transport(directory)
        created.append(child)
        backend.close()
        return child
    backend = CodexBackend(tmp_path, factory)
    with pytest.raises(CodexConnectionError, match='cancelled during startup'):
        backend.ask('Hello')
    assert created[0].closed
    assert backend._transport is None
    assert not backend.snapshot()['active']
