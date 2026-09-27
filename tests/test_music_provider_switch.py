from dataclasses import asdict, replace
import threading

import pytest

from interfayce.settings import AppSettings, load_settings, save_settings, set_music_provider
from interfayce.voice_service import VoiceRuntime


@pytest.fixture
def runtime(tmp_path, monkeypatch):
    monkeypatch.setenv("INTERFAYCE_SETTINGS_PATH", str(tmp_path / "settings.json"))
    runtime = VoiceRuntime.__new__(VoiceRuntime)
    runtime.command_lock = threading.Lock()
    return runtime


def test_wrist_switch_preserves_audio_and_other_settings(runtime):
    original = save_settings(replace(AppSettings(), tts_volume=.42, broadcast_gain_db=6,
                                    song_announce_enabled=False, tts_output="Headset"))
    selected = runtime.select_music_provider("youtube")
    assert selected == replace(original, music_provider="youtube")
    assert load_settings() == selected
    assert runtime.select_music_provider("spotify") == original


def test_repeated_selection_is_idempotent(runtime):
    first = runtime.select_music_provider("youtube")
    assert runtime.select_music_provider("youtube") == first


def test_active_voice_command_blocks_switch_without_changing_settings(runtime):
    original = save_settings(AppSettings())
    with runtime.command_lock:
        assert runtime.select_music_provider("youtube") is None
    assert load_settings() == original


def test_bad_selection_releases_lock_and_preserves_settings(runtime):
    before = save_settings(AppSettings())
    with pytest.raises(ValueError):
        runtime.select_music_provider("chrome")
    assert not runtime.command_lock.locked()
    assert asdict(load_settings()) == asdict(before)
