"""Deliberate, OVRAS-style playspace-drag gesture logic.

This module deliberately has no SteamVR calls.  It only decides whether an
input sequence is safe to hand to the eventual playspace adapter.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum


class DragPhase(StrEnum):
    IDLE = "idle"
    FIRST_PRESS = "first_press"
    DRAGGING = "dragging"


@dataclass(frozen=True)
class DragGesture:
    phase: DragPhase = DragPhase.IDLE
    last_release_seconds: float | None = None
    double_tap_window_seconds: float = 0.35

    def pressed(self, now_seconds: float) -> "DragGesture":
        """Handle a B-button press; only a timely second press enters drag."""
        if (
            self.phase is DragPhase.IDLE
            and self.last_release_seconds is not None
            and now_seconds - self.last_release_seconds <= self.double_tap_window_seconds
        ):
            return DragGesture(
                phase=DragPhase.DRAGGING,
                last_release_seconds=None,
                double_tap_window_seconds=self.double_tap_window_seconds,
            )
        return DragGesture(
            phase=DragPhase.FIRST_PRESS,
            last_release_seconds=self.last_release_seconds,
            double_tap_window_seconds=self.double_tap_window_seconds,
        )

    def released(self, now_seconds: float) -> "DragGesture":
        """End a drag, or record the first tap as a possible deliberate arm."""
        return DragGesture(
            phase=DragPhase.IDLE,
            last_release_seconds=now_seconds,
            double_tap_window_seconds=self.double_tap_window_seconds,
        )

    @property
    def is_dragging(self) -> bool:
        return self.phase is DragPhase.DRAGGING
