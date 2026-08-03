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
    stt_microphone: str = ""
    haptic_strength: float = 0.22
    spotify_client_id: str = ""
    llm_endpoint: str = "https://api.deepinfra.com/v1/openai"
    llm_model: str = "deepseek-ai/DeepSeek-V4-Flash"
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
        stt_microphone=str(settings.stt_microphone).strip(),
        haptic_strength=max(0.0, min(1.0, float(settings.haptic_strength))),
        spotify_client_id=str(settings.spotify_client_id).strip(),
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
                stt_microphone=data.get("stt_microphone", ""),
                haptic_strength=data.get("haptic_strength", 0.22),
                spotify_client_id=data.get("spotify_client_id", ""),
                llm_endpoint=data.get("llm_endpoint", "https://api.deepinfra.com/v1/openai"),
                llm_model=data.get("llm_model", "deepseek-ai/DeepSeek-V4-Flash"),
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
                    temperature: float = 0.65) -> AppSettings:
    return save_settings(replace(
        load_settings(),
        llm_endpoint=endpoint,
        llm_model=model,
        llm_reasoning_effort=reasoning_effort,
        llm_temperature=temperature,
    ))


def settings_wire_text(settings: AppSettings | None = None) -> str:
    current = settings or load_settings()
    return (f"{round(current.tts_volume * 100)}\t{int(current.tts_muted)}\t"
            f"{current.tts_speed:.2f}\t{current.haptic_strength:.2f}")
