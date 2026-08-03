"""Persistent, non-secret Interfayce settings.

Credentials deliberately do not belong here. The desktop settings UI stores
those through Windows credential protection instead.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace
import json
import os
from pathlib import Path
import threading


@dataclass(frozen=True)
class AppSettings:
    tts_volume: float = 0.85
    tts_muted: bool = False
    tts_speed: float = 1.0
    tts_endpoint: str = ""
    tts_model: str = ""
    tts_voice: str = ""
    tts_output: str = ""
    stt_microphone: str = ""
    haptic_strength: float = 0.22
    broadcast_gain_db: float = 12.0
    spotify_client_id: str = ""
    llm_enabled: bool = False
    llm_endpoint: str = ""
    llm_model: str = ""
    llm_reasoning_effort: str = ""
    llm_temperature: float = 0.65


_LOCK = threading.Lock()


def settings_path() -> Path:
    if configured := os.environ.get("INTERFAYCE_SETTINGS_PATH"):
        return Path(configured).expanduser()
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return local / "Interfayce" / "settings.json"


def _clamp(settings: AppSettings) -> AppSettings:
    return AppSettings(
        tts_volume=max(0.0, min(1.0, float(settings.tts_volume))),
        tts_muted=bool(settings.tts_muted),
        tts_speed=max(0.25, min(4.0, float(settings.tts_speed))),
        tts_endpoint=str(settings.tts_endpoint).strip().rstrip("/"),
        tts_model=str(settings.tts_model).strip(),
        tts_voice=str(settings.tts_voice).strip(),
        tts_output=str(settings.tts_output).strip(),
        stt_microphone=str(settings.stt_microphone).strip(),
        haptic_strength=max(0.0, min(1.0, float(settings.haptic_strength))),
        broadcast_gain_db=max(0.0, min(24.0, float(settings.broadcast_gain_db))),
        spotify_client_id=str(settings.spotify_client_id).strip(),
        llm_enabled=bool(settings.llm_enabled),
        llm_endpoint=str(settings.llm_endpoint).strip().rstrip("/"),
        llm_model=str(settings.llm_model).strip(),
        llm_reasoning_effort=str(settings.llm_reasoning_effort).strip(),
        llm_temperature=max(0.0, min(2.0, float(settings.llm_temperature))),
    )


def load_settings() -> AppSettings:
    with _LOCK:
        path = settings_path()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            return _clamp(AppSettings(
                tts_volume=data.get("tts_volume", 0.85),
                tts_muted=data.get("tts_muted", False),
                tts_speed=data.get("tts_speed", 1.0),
                tts_endpoint=data.get("tts_endpoint", ""),
                tts_model=data.get("tts_model", ""),
                tts_voice=data.get("tts_voice", ""),
                tts_output=data.get("tts_output", ""),
                stt_microphone=data.get("stt_microphone", ""),
                haptic_strength=data.get("haptic_strength", 0.22),
                broadcast_gain_db=data.get("broadcast_gain_db", 12.0),
                spotify_client_id=data.get("spotify_client_id", ""),
                llm_enabled=data.get("llm_enabled", False),
                llm_endpoint=data.get("llm_endpoint", ""),
                llm_model=data.get("llm_model", ""),
                llm_reasoning_effort=data.get("llm_reasoning_effort", ""),
                llm_temperature=data.get("llm_temperature", 0.65),
            ))
        except (FileNotFoundError, OSError, TypeError, ValueError, json.JSONDecodeError):
            return AppSettings()


def save_settings(settings: AppSettings) -> AppSettings:
    cleaned = _clamp(settings)
    with _LOCK:
        path = settings_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(json.dumps(asdict(cleaned), indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)
    return cleaned


def adjust_tts_volume(delta: float) -> AppSettings:
    current = load_settings()
    return save_settings(replace(current, tts_volume=current.tts_volume + delta))


def toggle_tts_mute() -> AppSettings:
    current = load_settings()
    return save_settings(replace(current, tts_muted=not current.tts_muted))


def adjust_broadcast_gain(delta_db: float) -> AppSettings:
    current = load_settings()
    return save_settings(replace(
        current, broadcast_gain_db=current.broadcast_gain_db + delta_db
    ))


def set_runtime_controls(*, tts_volume: float, tts_muted: bool,
                         stt_microphone: str, haptic_strength: float) -> AppSettings:
    return save_settings(replace(
        load_settings(),
        tts_volume=tts_volume,
        tts_muted=tts_muted,
        stt_microphone=stt_microphone,
        haptic_strength=haptic_strength,
    ))


def set_spotify_client_id(client_id: str) -> AppSettings:
    return save_settings(replace(load_settings(), spotify_client_id=client_id.strip()))


def set_llm_profile(*, endpoint: str, model: str, reasoning_effort: str = "",
                    temperature: float = 0.65, enabled: bool | None = None) -> AppSettings:
    current = load_settings()
    return save_settings(replace(
        current,
        llm_enabled=current.llm_enabled if enabled is None else enabled,
        llm_endpoint=endpoint,
        llm_model=model,
        llm_reasoning_effort=reasoning_effort,
        llm_temperature=temperature,
    ))


def set_desktop_configuration(*, tts_volume: float, tts_muted: bool,
                              tts_speed: float, tts_endpoint: str,
                              tts_model: str, tts_voice: str, tts_output: str,
                              stt_microphone: str, haptic_strength: float,
                              broadcast_gain_db: float, spotify_client_id: str,
                              llm_enabled: bool, llm_endpoint: str,
                              llm_model: str, llm_reasoning_effort: str,
                              llm_temperature: float) -> AppSettings:
    return save_settings(replace(
        load_settings(),
        tts_volume=tts_volume,
        tts_muted=tts_muted,
        tts_speed=tts_speed,
        tts_endpoint=tts_endpoint,
        tts_model=tts_model,
        tts_voice=tts_voice,
        tts_output=tts_output,
        stt_microphone=stt_microphone,
        haptic_strength=haptic_strength,
        broadcast_gain_db=broadcast_gain_db,
        spotify_client_id=spotify_client_id,
        llm_enabled=llm_enabled,
        llm_endpoint=llm_endpoint,
        llm_model=llm_model,
        llm_reasoning_effort=llm_reasoning_effort,
        llm_temperature=llm_temperature,
    ))


def settings_wire_text(settings: AppSettings | None = None) -> str:
    current = settings or load_settings()
    return (f"{round(current.tts_volume * 100)}\t{int(current.tts_muted)}\t"
            f"{current.tts_speed:.2f}\t{current.haptic_strength:.2f}\t"
            f"{current.broadcast_gain_db:.1f}")
