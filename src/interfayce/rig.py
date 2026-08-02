"""A personal body-rig battery board, independent of any one VR runtime."""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum


class RigSlot(StrEnum):
    LEFT_ELBOW = "left_elbow"
    RIGHT_ELBOW = "right_elbow"
    CHEST = "chest"
    HIP = "hip"
    LEFT_THIGH = "left_thigh"
    RIGHT_THIGH = "right_thigh"
    LEFT_FOOT = "left_foot"
    RIGHT_FOOT = "right_foot"
    LEFT_CONTROLLER = "left_controller"
    RIGHT_CONTROLLER = "right_controller"


@dataclass(frozen=True)
class BatteryReading:
    """A real reported battery state; unknown values stay unknown."""

    percent: float | None
    connected: bool
    charging: bool = False
    runtime_seconds: int | None = None


@dataclass(frozen=True)
class SlotBatteryState:
    slot: RigSlot
    percent: float | None
    connected: bool
    charging: bool
    runtime_seconds: int | None
    source_count: int


@dataclass(frozen=True)
class PersonalRigLayout:
    """Maps logical wrist-board slots to source tracker/device IDs.

    Each foot intentionally contains an ankle and foot tracker. A foot slot is
    only healthy when both are connected, and uses the earliest depletion as
    its displayed battery/runtime estimate.
    """

    sources: dict[RigSlot, tuple[str, ...]]

    def summarize(self, readings: dict[str, BatteryReading]) -> list[SlotBatteryState]:
        return [
            self._summarize_slot(slot, source_ids, readings)
            for slot, source_ids in self.sources.items()
        ]

    @staticmethod
    def _summarize_slot(
        slot: RigSlot,
        source_ids: tuple[str, ...],
        readings: dict[str, BatteryReading],
    ) -> SlotBatteryState:
        samples = [readings[source_id] for source_id in source_ids if source_id in readings]
        connected = len(samples) == len(source_ids) and all(sample.connected for sample in samples)
        known_percentages = [sample.percent for sample in samples if sample.percent is not None]
        known_runtimes = [
            sample.runtime_seconds for sample in samples if sample.runtime_seconds is not None
        ]
        return SlotBatteryState(
            slot=slot,
            percent=min(known_percentages) if known_percentages else None,
            connected=connected,
            charging=bool(samples) and all(sample.charging for sample in samples),
            runtime_seconds=min(known_runtimes) if known_runtimes else None,
            source_count=len(source_ids),
        )


DEFAULT_PERSONAL_RIG = PersonalRigLayout(
    sources={
        RigSlot.LEFT_ELBOW: ("left_elbow",),
        RigSlot.RIGHT_ELBOW: ("right_elbow",),
        RigSlot.CHEST: ("chest",),
        RigSlot.HIP: ("hip",),
        RigSlot.LEFT_THIGH: ("left_thigh",),
        RigSlot.RIGHT_THIGH: ("right_thigh",),
        RigSlot.LEFT_FOOT: ("left_ankle", "left_foot"),
        RigSlot.RIGHT_FOOT: ("right_ankle", "right_foot"),
        RigSlot.LEFT_CONTROLLER: ("left_controller",),
        RigSlot.RIGHT_CONTROLLER: ("right_controller",),
    }
)
