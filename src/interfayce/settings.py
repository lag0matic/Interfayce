"""Persistent, non-secret Interfayce settings.

Credentials deliberately do not belong here. The future desktop settings UI
will store those through Windows credential protection instead.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import threading


@dataclass(frozen=True)
class AppSettings:
    tts_volume: float = 0.85
    tts_muted: bool = False
    tts_speed: float = 1.0


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
    return save_settings(AppSettings(
        tts_volume=current.tts_volume + delta,
        tts_muted=current.tts_muted,
        tts_speed=current.tts_speed,
    ))


def toggle_tts_mute() -> AppSettings:
    current = load_settings()
    return save_settings(AppSettings(
        tts_volume=current.tts_volume,
        tts_muted=not current.tts_muted,
        tts_speed=current.tts_speed,
    ))


def settings_wire_text(settings: AppSettings | None = None) -> str:
    current = settings or load_settings()
    return f"{round(current.tts_volume * 100)}\t{int(current.tts_muted)}\t{current.tts_speed:.2f}"
