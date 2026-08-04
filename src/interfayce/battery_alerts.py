"""Quiet threshold-crossing battery announcements."""

from __future__ import annotations


class BatteryAlertMonitor:
    def __init__(self, *, low: int = 20, critical: int = 10) -> None:
        self.low = low
        self.critical = critical
        self._levels: dict[str, int] = {}

    def observe(self, readings: dict[str, int]) -> str | None:
        announcements: list[str] = []
        for name, raw_percent in readings.items():
            percent = max(0, min(100, int(raw_percent)))
            level = 2 if percent <= self.critical else 1 if percent <= self.low else 0
            previous = self._levels.get(name, -1)
            self._levels[name] = level
            if level <= 0 or level <= previous:
                continue
            if level == 2:
                announcements.append(f"{name} battery critical")
            else:
                announcements.append(f"{name} battery {percent} percent")
        return ". ".join(announcements) + "." if announcements else None
