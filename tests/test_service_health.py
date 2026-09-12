from types import SimpleNamespace
from unittest.mock import Mock, patch

from interfayce.service_health import ServiceHealth
from interfayce.remote_stt import RemoteSttTranscriber


def test_offline_remote_only_yellow_after_local_initializes():
    local = Mock()
    remote = RemoteSttTranscriber('http://127.0.0.1:5010', 'test', fallback=local)
    health = ServiceHealth(remote)
    with patch('interfayce.service_health.check_json', side_effect=OSError), \
         patch.object(remote, '_headers', return_value={}):
        assert health.check('STT')[0] == 'backup'
        assert health.check('STT')[0] == 'backup'
    local.warm.assert_called_once()


def test_failed_local_does_not_report_backup_ready():
    local = Mock()
    local.warm.side_effect = RuntimeError('model missing')
    health = ServiceHealth(RemoteSttTranscriber('http://127.0.0.1:5010', 'test', fallback=local))
    with patch('interfayce.service_health.check_json', side_effect=OSError), \
         patch.object(health.transcriber, '_headers', return_value={}):
        import pytest
        with pytest.raises(RuntimeError):
            health.check('STT')
    assert not health._local_ready


def test_stale_success_becomes_unknown_and_wire_has_fixed_services():
    health = ServiceHealth(Mock())
    with patch('interfayce.service_health.time.monotonic', return_value=100):
        health.set('TTS', 'good', 'Available')
        assert 'TTS\tgood\tAvailable' in health.wire()
    with patch('interfayce.service_health.time.monotonic', return_value=146):
        assert 'TTS\tunknown\t' in health.wire()
        assert len(health.wire().splitlines()) == 4


def test_muted_tts_does_not_contact_server():
    health = ServiceHealth(Mock())
    with patch('interfayce.service_health.load_settings', return_value=SimpleNamespace(
            tts_endpoint='http://localhost:5000/v1/audio/speech', tts_muted=True, tts_volume=1)), \
         patch('interfayce.service_health.check_json') as request:
        assert health.check('TTS') == ('unknown', 'Muted')
        request.assert_not_called()
