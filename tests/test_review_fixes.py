import asyncio
import os
import subprocess
import sys
from types import SimpleNamespace

import pytest

from interfayce.configured_stt import ConfiguredTranscriber
from interfayce.playback_controls import PlaybackControls
from interfayce.settings import AppSettings, save_settings, load_settings
from interfayce.voice import parse_music_intent, execute_music_intent


def test_settings_updates_from_separate_processes_are_atomic(tmp_path, monkeypatch):
    monkeypatch.setenv('INTERFAYCE_SETTINGS_PATH', str(tmp_path / 'settings.json'))
    save_settings(AppSettings(broadcast_gain_db=0))
    script = 'from interfayce.settings import adjust_broadcast_gain\nfor _ in range(20): adjust_broadcast_gain(0.1)'
    children = [subprocess.Popen([sys.executable, '-c', script], env=os.environ.copy()) for _ in range(3)]
    for child in children:
        assert child.wait(timeout=30) == 0
    assert load_settings().broadcast_gain_db == pytest.approx(6.0)


def test_stt_route_updates_but_capture_fallback_retains_original_provider():
    config = SimpleNamespace(stt_endpoint='server-one', stt_model='one')
    class Provider:
        def __init__(self, endpoint, model, **_): self.name = endpoint + '/' + model
        def start_stream(self, callback): return self.name
        def transcribe(self, audio): return self.name
    local = Provider('local', 'local')
    router = ConfiguredTranscriber(local, settings=lambda: config, remote_factory=Provider)
    assert router.start_stream(None) == 'server-one/one'
    config.stt_endpoint, config.stt_model = 'server-two', 'two'
    assert router.transcribe(None) == 'server-one/one'
    assert router.transcribe(None) == 'server-two/two'
    config.stt_endpoint = ''
    assert router.transcribe(None) == 'local/local'


def test_live_playback_controls_refresh_during_utterance():
    value = AppSettings()
    clock = [0.0]
    controls = PlaybackControls(source=lambda: value, clock=lambda: clock[0])
    assert not controls.current().tts_muted
    value = AppSettings(tts_muted=True)
    clock[0] = 0.2
    assert controls.current().tts_muted


def test_repeated_pause_and_resume_are_idempotent():
    class Media:
        playing = False
        async def play(self): self.playing = True; return True
        async def pause(self): self.playing = False; return True
    media = Media()
    for words, expected in [('pause music', False), ('resume music', True),
                            ('resume music', True), ('pause music', False), ('pause music', False)]:
        assert asyncio.run(execute_music_intent(parse_music_intent(words), media)).succeeded
        assert media.playing is expected
