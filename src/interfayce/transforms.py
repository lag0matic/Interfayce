"""Small, dependency-free 3x4 rigid-transform helpers for playspace operations."""

from __future__ import annotations

from typing import TypeAlias


Matrix34: TypeAlias = tuple[tuple[float, float, float, float], ...]
Vector3: TypeAlias = tuple[float, float, float]


def translation_delta(start: Vector3, current: Vector3) -> Vector3:
    """Return the physical hand movement between two sampled controller poses."""
    return tuple(
        current_axis - start_axis
        for start_axis, current_axis in zip(start, current, strict=True)
    )


def translated_origin(baseline: Matrix34, offset: Vector3) -> Matrix34:
    """Produce a draft origin with a local translation; does not write SteamVR."""
    if len(baseline) != 3 or any(len(row) != 4 for row in baseline):
        raise ValueError("Expected a 3x4 OpenVR origin matrix.")
    return tuple(
        tuple(
            value + offset[row_index] if column_index == 3 else value
            for column_index, value in enumerate(row)
        )
        for row_index, row in enumerate(baseline)
    )
