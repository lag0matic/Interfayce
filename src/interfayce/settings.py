"""Persistent, non-secret Interfayce settings.

Credentials deliberately do not belong here. The desktop settings UI stores
those through Windows credential protection instead.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace
import json
import os
from pathlib import Path
import re
import threading

DEFAULT_COMMS_SHORTCUTS: tuple[tuple[str, str], ...] = (("", ""),) * 4
DEFAULT_DESKTOP_FAVORITES: tuple[tuple[str, str], ...] = (("", ""),) * 3
MAX_DESKTOP_HISTORY = 8


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
    stt_endpoint: str = ""
    stt_model: str = "whisper-turbo"
    comms_silence_timeout_seconds: float = 3.0
    haptic_strength: float = 0.22
    broadcast_gain_db: float = 12.0
    playspace_travel_limit_meters: float = 10.0
    spotify_client_id: str = ""
    llm_enabled: bool = False
    llm_endpoint: str = ""
    llm_model: str = ""
    llm_reasoning_effort: str = ""
    llm_temperature: float = 0.65
    comms_shortcuts: tuple[tuple[str, str], ...] = DEFAULT_COMMS_SHORTCUTS
    desktop_favorites: tuple[tuple[str, str], ...] = DEFAULT_DESKTOP_FAVORITES
    wrist_hand: str = "left"
    wrist_offset_x: float = 0.0
    wrist_offset_y: float = 0.0
    wrist_offset_z: float = 0.0
    wrist_pitch: float = 0.0
    wrist_yaw: float = 0.0
    wrist_roll: float = 0.0


_LOCK = threading.RLock()


def settings_path() -> Path:
    if configured := os.environ.get("INTERFAYCE_SETTINGS_PATH"):
        return Path(configured).expanduser()
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return local / "Interfayce" / "settings.json"


def desktop_history_path() -> Path:
    if configured := os.environ.get("INTERFAYCE_DESKTOP_HISTORY_PATH"):
        return Path(configured).expanduser()
    return settings_path().with_name("desktop-history.json")


def _clean_desktop_target(label: object, executable: object) -> tuple[str, str]:
    clean_label = " ".join(str(label).split())[:14]
    clean_executable = str(executable).strip()[:1024]
    path = Path(clean_executable) if clean_executable else None
    is_executable = (path is not None and path.is_absolute()
                     and path.suffix.casefold() == ".exe")
    is_app_id = bool(re.fullmatch(r"aumid:[A-Za-z0-9._-]+![A-Za-z0-9._-]+",
                                  clean_executable))
    if not clean_label or (not is_executable and not is_app_id):
        return "", ""
    return clean_label, clean_executable


def _desktop_target_identity(executable: str) -> str:
    """Match launch targets and captured paths that represent the same app."""

    cleaned = executable.strip()
    if not cleaned:
        return ""
    folded = cleaned.casefold()
    if folded.startswith("aumid:"):
        package_family, separator, _application_id = folded[6:].partition("!")
        return f"package:{package_family}" if separator and package_family else ""

    # Captured Store applications expose the executable inside a versioned
    # WindowsApps package directory, while configured favorites use an AUMID.
    # Package full names are name_version_architecture_resource_publisher;
    # package family names are name_publisher.
    parts = Path(cleaned).parts
    for index, part in enumerate(parts[:-1]):
        if part.casefold() != "windowsapps" or index + 1 >= len(parts):
            continue
        package_parts = parts[index + 1].split("_")
        if len(package_parts) >= 5 and package_parts[0] and package_parts[-1]:
            return f"package:{package_parts[0].casefold()}_{package_parts[-1].casefold()}"

    normalized = os.path.normcase(os.path.normpath(cleaned))
    path = Path(normalized)
    # Electron/Squirrel installs such as Discord move between app-version
    # directories. Treat those versions as one application history entry.
    if re.fullmatch(r"app-[0-9.]+", path.parent.name, flags=re.IGNORECASE):
        normalized = os.path.normcase(os.path.normpath(str(path.parent.parent / path.name)))
    return f"path:{normalized.casefold()}"


def _clamp(settings: AppSettings) -> AppSettings:
    shortcuts: list[tuple[str, str]] = []
    for item in tuple(settings.comms_shortcuts)[:4]:
        try:
            label, message = item
        except (TypeError, ValueError):
            continue
        clean_label = " ".join(str(label).split())[:12]
        clean_message = " ".join(str(message).split())[:144]
        shortcuts.append((clean_label, clean_message))
    shortcuts.extend((("", ""),) * (4 - len(shortcuts)))
    favorites: list[tuple[str, str]] = []
    for item in tuple(settings.desktop_favorites)[:3]:
        try:
            label, executable = item
        except (TypeError, ValueError):
            continue
        clean_label, clean_executable = _clean_desktop_target(label, executable)
        favorites.append((clean_label, clean_executable))
    favorites.extend((("", ""),) * (3 - len(favorites)))
    return AppSettings(
        tts_volume=max(0.0, min(1.0, float(settings.tts_volume))),
        tts_muted=bool(settings.tts_muted),
        tts_speed=max(0.25, min(4.0, float(settings.tts_speed))),
        tts_endpoint=str(settings.tts_endpoint).strip().rstrip("/"),
        tts_model=str(settings.tts_model).strip(),
        tts_voice=str(settings.tts_voice).strip(),
        tts_output=str(settings.tts_output).strip(),
        stt_microphone=str(settings.stt_microphone).strip(),
        stt_endpoint=str(settings.stt_endpoint).strip().rstrip("/"),
        stt_model=str(settings.stt_model).strip() or "whisper-turbo",
        comms_silence_timeout_seconds=max(
            1.0, min(30.0, float(settings.comms_silence_timeout_seconds))),
        haptic_strength=max(0.0, min(1.0, float(settings.haptic_strength))),
        broadcast_gain_db=max(0.0, min(24.0, float(settings.broadcast_gain_db))),
        playspace_travel_limit_meters=max(
            1.0, min(50.0, float(settings.playspace_travel_limit_meters))),
        spotify_client_id=str(settings.spotify_client_id).strip(),
        llm_enabled=bool(settings.llm_enabled),
        llm_endpoint=str(settings.llm_endpoint).strip().rstrip("/"),
        llm_model=str(settings.llm_model).strip(),
        llm_reasoning_effort=str(settings.llm_reasoning_effort).strip(),
        llm_temperature=max(0.0, min(2.0, float(settings.llm_temperature))),
        comms_shortcuts=tuple(shortcuts),
        desktop_favorites=tuple(favorites),
        wrist_hand="right" if str(settings.wrist_hand).strip().casefold() == "right" else "left",
        wrist_offset_x=max(-0.10, min(0.10, float(settings.wrist_offset_x))),
        wrist_offset_y=max(-0.10, min(0.10, float(settings.wrist_offset_y))),
        wrist_offset_z=max(-0.10, min(0.10, float(settings.wrist_offset_z))),
        wrist_pitch=max(-45.0, min(45.0, float(settings.wrist_pitch))),
        wrist_yaw=max(-45.0, min(45.0, float(settings.wrist_yaw))),
        wrist_roll=max(-45.0, min(45.0, float(settings.wrist_roll))),
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
                stt_endpoint=data.get("stt_endpoint", ""),
                stt_model=data.get("stt_model", "whisper-turbo"),
                comms_silence_timeout_seconds=data.get("comms_silence_timeout_seconds", 3.0),
                haptic_strength=data.get("haptic_strength", 0.22),
                broadcast_gain_db=data.get("broadcast_gain_db", 12.0),
                playspace_travel_limit_meters=data.get(
                    "playspace_travel_limit_meters", 10.0),
                spotify_client_id=data.get("spotify_client_id", ""),
                llm_enabled=data.get("llm_enabled", False),
                llm_endpoint=data.get("llm_endpoint", ""),
                llm_model=data.get("llm_model", ""),
                llm_reasoning_effort=data.get("llm_reasoning_effort", ""),
                llm_temperature=data.get("llm_temperature", 0.65),
                comms_shortcuts=tuple(tuple(item) for item in
                    data.get("comms_shortcuts", DEFAULT_COMMS_SHORTCUTS)),
                desktop_favorites=tuple(tuple(item) for item in
                    data.get("desktop_favorites", DEFAULT_DESKTOP_FAVORITES)),
                wrist_hand=data.get("wrist_hand", "left"),
                wrist_offset_x=data.get("wrist_offset_x", 0.0),
                wrist_offset_y=data.get("wrist_offset_y", 0.0),
                wrist_offset_z=data.get("wrist_offset_z", 0.0),
                wrist_pitch=data.get("wrist_pitch", 0.0),
                wrist_yaw=data.get("wrist_yaw", 0.0),
                wrist_roll=data.get("wrist_roll", 0.0),
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


def load_desktop_history() -> tuple[tuple[str, str], ...]:
    with _LOCK:
        try:
            raw = json.loads(desktop_history_path().read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, TypeError, ValueError, json.JSONDecodeError):
            return ()
    history: list[tuple[str, str]] = []
    seen: set[str] = set()
    for item in raw if isinstance(raw, list) else ():
        try:
            label, executable = item
        except (TypeError, ValueError):
            continue
        cleaned = _clean_desktop_target(label, executable)
        identity = cleaned[1].casefold()
        if not identity or identity in seen:
            continue
        history.append(cleaned)
        seen.add(identity)
        if len(history) >= MAX_DESKTOP_HISTORY:
            break
    return tuple(history)


def record_desktop_recent(label: str, executable: str) -> tuple[tuple[str, str], ...]:
    cleaned = _clean_desktop_target(label, executable)
    if not cleaned[0] or not cleaned[1]:
        raise ValueError("Desktop recent target is invalid.")
    identity = _desktop_target_identity(cleaned[1])
    with _LOCK:
        history = [item for item in load_desktop_history()
                   if _desktop_target_identity(item[1]) != identity]
        history.insert(0, cleaned)
        history = history[:MAX_DESKTOP_HISTORY]
        path = desktop_history_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(json.dumps(history, indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)
    return tuple(history)


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
                              stt_microphone: str, comms_silence_timeout_seconds: float,
                              haptic_strength: float,
                              broadcast_gain_db: float, spotify_client_id: str,
                              llm_enabled: bool, llm_endpoint: str,
                              llm_model: str, llm_reasoning_effort: str,
                              llm_temperature: float,
                              comms_shortcuts: tuple[tuple[str, str], ...] | None = None,
                              desktop_favorites: tuple[tuple[str, str], ...] | None = None,
                              wrist_hand: str | None = None,
                              wrist_offset_x: float | None = None,
                              wrist_offset_y: float | None = None,
                              wrist_offset_z: float | None = None,
                              wrist_pitch: float | None = None,
                              wrist_yaw: float | None = None,
                              wrist_roll: float | None = None,
                              stt_endpoint: str | None = None,
                              stt_model: str | None = None,
                              playspace_travel_limit_meters: float | None = None) -> AppSettings:
    current = load_settings()
    return save_settings(replace(
        current,
        tts_volume=tts_volume,
        tts_muted=tts_muted,
        tts_speed=tts_speed,
        tts_endpoint=tts_endpoint,
        tts_model=tts_model,
        tts_voice=tts_voice,
        tts_output=tts_output,
        stt_microphone=stt_microphone,
        stt_endpoint=current.stt_endpoint if stt_endpoint is None else stt_endpoint,
        stt_model=current.stt_model if stt_model is None else stt_model,
        comms_silence_timeout_seconds=comms_silence_timeout_seconds,
        haptic_strength=haptic_strength,
        broadcast_gain_db=broadcast_gain_db,
        playspace_travel_limit_meters=(current.playspace_travel_limit_meters
            if playspace_travel_limit_meters is None else playspace_travel_limit_meters),
        spotify_client_id=spotify_client_id,
        llm_enabled=llm_enabled,
        llm_endpoint=llm_endpoint,
        llm_model=llm_model,
        llm_reasoning_effort=llm_reasoning_effort,
        llm_temperature=llm_temperature,
        comms_shortcuts=current.comms_shortcuts if comms_shortcuts is None else comms_shortcuts,
        desktop_favorites=current.desktop_favorites if desktop_favorites is None else desktop_favorites,
        wrist_hand=current.wrist_hand if wrist_hand is None else wrist_hand,
        wrist_offset_x=current.wrist_offset_x if wrist_offset_x is None else wrist_offset_x,
        wrist_offset_y=current.wrist_offset_y if wrist_offset_y is None else wrist_offset_y,
        wrist_offset_z=current.wrist_offset_z if wrist_offset_z is None else wrist_offset_z,
        wrist_pitch=current.wrist_pitch if wrist_pitch is None else wrist_pitch,
        wrist_yaw=current.wrist_yaw if wrist_yaw is None else wrist_yaw,
        wrist_roll=current.wrist_roll if wrist_roll is None else wrist_roll,
    ))


def comms_shortcut_labels(settings: AppSettings | None = None) -> str:
    current = settings or load_settings()
    return "\t".join(label if label and message else "" for label, message in current.comms_shortcuts)


def desktop_favorites_wire_text(
        settings: AppSettings | None = None,
        history: tuple[tuple[str, str], ...] | None = None) -> str:
    current = settings or load_settings()
    effective = list(current.desktop_favorites)
    configured = {_desktop_target_identity(executable) for label, executable in effective
                  if label and executable}
    configured.discard("")
    recent = load_desktop_history() if history is None else history
    recent_index = 0
    for slot, (label, executable) in enumerate(effective):
        if label and executable:
            continue
        while recent_index < len(recent):
            recent_label, recent_executable = _clean_desktop_target(*recent[recent_index])
            recent_index += 1
            identity = _desktop_target_identity(recent_executable)
            if identity and identity not in configured:
                effective[slot] = (recent_label, recent_executable)
                configured.add(identity)
                break
    return "\n".join(
        f"{label}\t{executable}" if label and executable else "\t"
        for label, executable in effective
    )


def settings_wire_text(settings: AppSettings | None = None) -> str:
    current = settings or load_settings()
    return (f"{round(current.tts_volume * 100)}\t{int(current.tts_muted)}\t"
            f"{current.tts_speed:.2f}\t{current.haptic_strength:.2f}\t"
            f"{current.broadcast_gain_db:.1f}\t{current.wrist_hand}\t"
            f"{current.wrist_offset_x:.3f}\t{current.wrist_offset_y:.3f}\t"
            f"{current.wrist_offset_z:.3f}\t{current.wrist_pitch:.1f}\t"
            f"{current.wrist_yaw:.1f}\t{current.wrist_roll:.1f}\t"
            f"{current.playspace_travel_limit_meters:.1f}")
