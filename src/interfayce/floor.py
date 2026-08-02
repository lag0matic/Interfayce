"""Pure floor-measurement logic; it deliberately cannot write SteamVR state."""

from __future__ import annotations

from dataclasses import dataclass
from statistics import fmean, pstdev


Position = tuple[float, float, float]


@dataclass(frozen=True)
class FloorMeasurement:
    controller: str
    position: Position
    positional_spread_m: Position
    sample_count: int

    @property
    def stable(self) -> bool:
        """Reject a controller that moved more than 5 mm during sampling."""
        return max(self.positional_spread_m) <= 0.005


def summarize_floor_measurement(samples: dict[str, list[Position]]) -> FloorMeasurement | None:
    """Choose the lower controller and summarize its positional stability."""
    candidates: list[FloorMeasurement] = []
    for controller, points in samples.items():
        if not points:
            continue
        axes = list(zip(*points, strict=True))
        candidates.append(
            FloorMeasurement(
                controller=controller,
                position=tuple(fmean(axis) for axis in axes),
                positional_spread_m=tuple(pstdev(axis) for axis in axes),
                sample_count=len(points),
            )
        )
    return min(candidates, key=lambda measurement: measurement.position[1], default=None)
