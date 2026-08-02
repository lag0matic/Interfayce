"""Small SteamVR/OpenVR telemetry adapter for the Index controllers."""

from __future__ import annotations

from dataclasses import dataclass

from .rig import BatteryReading


@dataclass(frozen=True)
class SteamVrControllerStatus:
    readings: dict[str, BatteryReading]
    available: bool
    detail: str | None = None


@dataclass(frozen=True)
class SteamVrStandingOrigin:
    matrix: tuple[tuple[float, float, float, float], ...] | None
    available: bool
    detail: str | None = None


@dataclass(frozen=True)
class SteamVrControllerPose:
    connected: bool
    tracked: bool
    position: tuple[float, float, float] | None = None


@dataclass(frozen=True)
class SteamVrControllerPoseStatus:
    poses: dict[str, SteamVrControllerPose]
    available: bool
    detail: str | None = None


@dataclass(frozen=True)
class SteamVrButtonStatus:
    pressed: dict[str, bool]
    available: bool
    detail: str | None = None


def _read_controller_battery(openvr: object, system: object, role: int) -> BatteryReading:
    device_index = system.getTrackedDeviceIndexForControllerRole(role)
    if device_index == openvr.k_unTrackedDeviceIndexInvalid:
        return BatteryReading(percent=None, connected=False)
    if not system.isTrackedDeviceConnected(device_index):
        return BatteryReading(percent=None, connected=False)

    fraction = system.getFloatTrackedDeviceProperty(
        device_index, openvr.Prop_DeviceBatteryPercentage_Float
    )
    charging = system.getBoolTrackedDeviceProperty(
        device_index, openvr.Prop_DeviceIsCharging_Bool
    )
    return BatteryReading(percent=round(float(fraction) * 100, 1), connected=True, charging=charging)


def _read_controller_pose(
    openvr: object, system: object, poses: object, role: int
) -> SteamVrControllerPose:
    device_index = system.getTrackedDeviceIndexForControllerRole(role)
    if device_index == openvr.k_unTrackedDeviceIndexInvalid:
        return SteamVrControllerPose(connected=False, tracked=False)
    if not system.isTrackedDeviceConnected(device_index):
        return SteamVrControllerPose(connected=False, tracked=False)

    pose = poses[device_index]
    if not pose.bPoseIsValid:
        return SteamVrControllerPose(connected=True, tracked=False)
    matrix = pose.mDeviceToAbsoluteTracking
    return SteamVrControllerPose(
        connected=True,
        tracked=True,
        position=(float(matrix.m[0][3]), float(matrix.m[1][3]), float(matrix.m[2][3])),
    )


def read_index_controller_status() -> SteamVrControllerStatus:
    """Read the left/right controller state without assuming SteamVR is alive."""
    try:
        import openvr

        openvr.init(openvr.VRApplication_Background)
        try:
            system = openvr.VRSystem()
            return SteamVrControllerStatus(
                readings={
                    "left_controller": _read_controller_battery(
                        openvr, system, openvr.TrackedControllerRole_LeftHand
                    ),
                    "right_controller": _read_controller_battery(
                        openvr, system, openvr.TrackedControllerRole_RightHand
                    ),
                },
                available=True,
            )
        finally:
            openvr.shutdown()
    except Exception as error:  # OpenVR supplies platform-specific exception types.
        detail = str(error) or type(error).__name__
        return SteamVrControllerStatus(readings={}, available=False, detail=detail)


def read_index_controller_poses() -> SteamVrControllerPoseStatus:
    """Read controller positions in SteamVR's standing universe; never writes state."""
    try:
        import openvr

        openvr.init(openvr.VRApplication_Background)
        try:
            system = openvr.VRSystem()
            poses = system.getDeviceToAbsoluteTrackingPose(
                openvr.TrackingUniverseStanding,
                0,
                openvr.k_unMaxTrackedDeviceCount,
            )
            return SteamVrControllerPoseStatus(
                poses={
                    "left_controller": _read_controller_pose(
                        openvr, system, poses, openvr.TrackedControllerRole_LeftHand
                    ),
                    "right_controller": _read_controller_pose(
                        openvr, system, poses, openvr.TrackedControllerRole_RightHand
                    ),
                },
                available=True,
            )
        finally:
            openvr.shutdown()
    except Exception as error:
        detail = str(error) or type(error).__name__
        return SteamVrControllerPoseStatus(poses={}, available=False, detail=detail)


def read_index_b_buttons() -> SteamVrButtonStatus:
    """Read the physical Index B buttons directly; no action manifest required."""
    try:
        import openvr

        openvr.init(openvr.VRApplication_Background)
        try:
            system = openvr.VRSystem()
            return read_index_b_buttons_from_system(openvr, system)
        finally:
            openvr.shutdown()
    except Exception as error:
        detail = str(error) or type(error).__name__
        return SteamVrButtonStatus(pressed={}, available=False, detail=detail)


def read_index_b_buttons_from_system(openvr: object, system: object) -> SteamVrButtonStatus:
    """Read B state from an already-open OpenVR session."""
    bit = 1 << openvr.k_EButton_IndexController_B
    pressed: dict[str, bool] = {}
    for label, role in (
        ("left_controller", openvr.TrackedControllerRole_LeftHand),
        ("right_controller", openvr.TrackedControllerRole_RightHand),
    ):
        device_index = system.getTrackedDeviceIndexForControllerRole(role)
        if device_index == openvr.k_unTrackedDeviceIndexInvalid:
            pressed[label] = False
            continue
        valid, state = system.getControllerState(device_index)
        pressed[label] = bool(valid and state.ulButtonPressed & bit)
    return SteamVrButtonStatus(pressed=pressed, available=True)


def matrix_rows(matrix: object) -> tuple[tuple[float, float, float, float], ...]:
    """Convert OpenVR's 3x4 ctypes matrix to an immutable Python snapshot."""
    return tuple(tuple(float(value) for value in row) for row in matrix.m)


def read_standing_origin() -> SteamVrStandingOrigin:
    """Read SteamVR's working standing origin without altering the room setup."""
    try:
        import openvr

        openvr.init(openvr.VRApplication_Background)
        try:
            success, matrix = (
                openvr.VRChaperoneSetup().getWorkingStandingZeroPoseToRawTrackingPose()
            )
            if not success:
                return SteamVrStandingOrigin(
                    matrix=None,
                    available=False,
                    detail="SteamVR has no working standing-origin transform right now.",
                )
            return SteamVrStandingOrigin(matrix=matrix_rows(matrix), available=True)
        finally:
            openvr.shutdown()
    except Exception as error:  # OpenVR supplies platform-specific exception types.
        detail = str(error) or type(error).__name__
        return SteamVrStandingOrigin(matrix=None, available=False, detail=detail)
