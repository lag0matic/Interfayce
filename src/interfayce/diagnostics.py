"""Small, non-invasive first-run checks for the desktop settings window."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path

from .app_info import APP_VERSION
from .settings import AppSettings


@dataclass(frozen=True)
class DiagnosticResult:
    name: str
    state: str
    detail: str


@dataclass(frozen=True)
class DiagnosticReport:
    version: str
    checked_at: str
    results: tuple[DiagnosticResult, ...]

    @property
    def attention_count(self) -> int:
        return sum(result.state == "attention" for result in self.results)


def diagnostics_path() -> Path:
    if configured := os.environ.get("INTERFAYCE_DIAGNOSTICS_PATH"):
        return Path(configured).expanduser()
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return local / "Interfayce" / "diagnostics.json"


def run_diagnostics(settings: AppSettings, *, voice_service_ready: bool,
                    input_devices: list[str], output_devices: list[str],
                    install_root: Path | None = None) -> DiagnosticReport:
    results: list[DiagnosticResult] = []
    if voice_service_ready:
        results.append(DiagnosticResult("Voice service", "ready", "Resident service is responding."))
    else:
        results.append(DiagnosticResult(
            "Voice service", "attention", "Resident service is offline; Music voice and Comms STT are unavailable."))

    results.append(DiagnosticResult(
        "Audio input", "ready" if input_devices else "attention",
        f"{len(input_devices)} input device(s) detected." if input_devices
        else "No microphone input devices were detected."))
    results.append(DiagnosticResult(
        "Audio output", "ready" if output_devices else "attention",
        f"{len(output_devices)} output device(s) detected." if output_devices
        else "No spoken-response output devices were detected."))

    cable = any("cable" in name.casefold() for name in output_devices)
    results.append(DiagnosticResult(
        "Spotify broadcast", "ready" if cable else "optional",
        "A virtual cable output is available." if cable
        else "No VB-CABLE-style output found; only Spotify-to-VRChat broadcast is unavailable."))
    results.append(DiagnosticResult(
        "Kokoro", "ready" if settings.tts_endpoint else "optional",
        "Speech endpoint is configured." if settings.tts_endpoint
        else "Not configured; spoken acknowledgements and battery warnings stay silent."))
    results.append(DiagnosticResult(
        "Spotify OAuth", "ready" if settings.spotify_client_id else "optional",
        "Client ID is configured." if settings.spotify_client_id
        else "Not configured; local transport controls remain available."))
    results.append(DiagnosticResult(
        "LLM fallback", "ready" if settings.llm_enabled else "optional",
        "Enabled for unrecognized Music commands." if settings.llm_enabled
        else "Disabled; deterministic Music commands remain available."))

    if install_root is not None and (install_root / "InterfayceOverlay.exe").exists():
        required = ("InterfayceOverlay.exe", "InterfayceAudioEngine.exe", "openvr_api.dll")
        missing = [name for name in required if not (install_root / name).exists()]
        results.append(DiagnosticResult(
            "Installed files", "attention" if missing else "ready",
            f"Missing: {', '.join(missing)}" if missing else "Core installed files are present."))
    else:
        results.append(DiagnosticResult("Build type", "optional", "Running from the development workspace."))

    return DiagnosticReport(
        APP_VERSION,
        datetime.now(timezone.utc).isoformat(timespec="seconds"),
        tuple(results),
    )


def save_report(report: DiagnosticReport) -> None:
    path = diagnostics_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(asdict(report), indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def load_report() -> DiagnosticReport | None:
    try:
        payload = json.loads(diagnostics_path().read_text(encoding="utf-8"))
        results = tuple(DiagnosticResult(**item) for item in payload["results"])
        return DiagnosticReport(str(payload["version"]), str(payload["checked_at"]), results)
    except (FileNotFoundError, OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None


def needs_first_run(report: DiagnosticReport | None, *, installed: bool = False) -> bool:
    if report is None or report.version != APP_VERSION:
        return True
    prior_installed = any(result.name == "Installed files" for result in report.results)
    return prior_installed != installed
