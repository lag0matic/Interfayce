"""Quiet threshold-crossing battery announcements."""

from __future__ import annotations

from dataclasses import dataclass
from threading import Lock


@dataclass
class _AlertState:
    low_announced: bool = False
    critical_announced: bool = False


class BatteryAlertMonitor:
    def __init__(self, *, low: int = 20, critical: int = 10, recovery_margin: int = 5) -> None:
        self.low = low
        self.critical = critical
        self.recovery_margin = max(1, recovery_margin)
        self._states: dict[str, _AlertState] = {}
        self._lock = Lock()

    def observe(self, readings: dict[str, int]) -> str | None:
        announcements: list[str] = []
        with self._lock:
            for name, raw_percent in readings.items():
                percent = max(0, min(100, int(raw_percent)))
                state = self._states.setdefault(name, _AlertState())

                # SlimeVR's voltage-derived percentages can hover around a
                # boundary. A warning remains latched until the battery has
                # recovered well above that boundary, rather than re-arming on
                # a one-percent rebound.
                if percent > self.low + self.recovery_margin:
                    state.low_announced = False
                if percent > self.critical + self.recovery_margin:
                    state.critical_announced = False

                if percent <= self.critical:
                    if not state.critical_announced:
                        announcements.append(f"{name} battery critical")
                    state.critical_announced = True
                    # A device first observed as critical should not later emit
                    # the less urgent low warning while still below 20 percent.
                    state.low_announced = True
                elif percent <= self.low and not state.low_announced:
                    announcements.append(f"{name} battery {percent} percent")
                    state.low_announced = True
        return ". ".join(announcements) + "." if announcements else None
