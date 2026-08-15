"""Public build identity and explicit update discovery."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def _version_tuple(value: str) -> tuple[int, ...]:
    clean = value.strip().removeprefix("v").split("-", 1)[0]
    try:
        return tuple(int(part) for part in clean.split("."))
    except ValueError:
        return ()


def _read_version() -> str:
    candidates = [Path(__file__).resolve().parents[2] / "VERSION"]
    if bundle_root := getattr(sys, "_MEIPASS", ""):
        candidates.insert(0, Path(bundle_root) / "VERSION")
    for candidate in candidates:
        try:
            value = candidate.read_text(encoding="utf-8").strip()
            if _version_tuple(value):
                return value
        except OSError:
            continue
    return "1.2.0"


APP_VERSION = _read_version()
REPOSITORY_URL = "https://github.com/lag0matic/Interfayce"
RELEASES_URL = f"{REPOSITORY_URL}/releases"
LATEST_RELEASE_API = "https://api.github.com/repos/lag0matic/Interfayce/releases/latest"


@dataclass(frozen=True)
class UpdateResult:
    state: str
    message: str
    version: str = ""
    url: str = RELEASES_URL
def check_for_update(*, timeout: float = 4.0) -> UpdateResult:
    """Query GitHub only after an explicit user action."""
    request = Request(
        LATEST_RELEASE_API,
        headers={"Accept": "application/vnd.github+json", "User-Agent": "Interfayce"},
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        if error.code == 404:
            return UpdateResult("current", "No published release is newer than this build.")
        return UpdateResult("error", f"GitHub returned HTTP {error.code}.")
    except (OSError, URLError, ValueError, json.JSONDecodeError) as error:
        return UpdateResult("error", f"Update check failed: {error}")

    latest = str(payload.get("tag_name", "")).strip().removeprefix("v")
    release_url = str(payload.get("html_url", RELEASES_URL)) or RELEASES_URL
    if not latest or not _version_tuple(latest):
        return UpdateResult("error", "The latest GitHub release has no usable version tag.")
    if _version_tuple(latest) > _version_tuple(APP_VERSION):
        return UpdateResult("available", f"Interfayce {latest} is available.", latest, release_url)
    return UpdateResult("current", f"Interfayce {APP_VERSION} is current.", latest, release_url)
