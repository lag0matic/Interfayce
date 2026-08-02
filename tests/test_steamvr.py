import unittest

from interfayce.floor import summarize_floor_measurement
from interfayce.steamvr import (
    SteamVrStandingOrigin,
    _read_controller_battery,
    _read_controller_pose,
    matrix_rows,
)


class FakeOpenVr:
    k_unTrackedDeviceIndexInvalid = 99
    Prop_DeviceBatteryPercentage_Float = 1
    Prop_DeviceIsCharging_Bool = 2


class FakeSystem:
    def __init__(self, *, index: int = 4, connected: bool = True) -> None:
        self.index = index
        self.connected = connected

    def getTrackedDeviceIndexForControllerRole(self, role: int) -> int:
        return self.index

    def isTrackedDeviceConnected(self, index: int) -> bool:
        return self.connected

    def getFloatTrackedDeviceProperty(self, index: int, prop: int) -> float:
        return 0.735

    def getBoolTrackedDeviceProperty(self, index: int, prop: int) -> bool:
        return True


class FakePose:
    def __init__(self, *, valid: bool = True) -> None:
        self.bPoseIsValid = valid

        class Matrix:
            m = ((1, 0, 0, 1.25), (0, 1, 0, 0.5), (0, 0, 1, -2.75))

        self.mDeviceToAbsoluteTracking = Matrix()


class SteamVrTests(unittest.TestCase):
    def test_reads_percent_and_charging_state(self) -> None:
        reading = _read_controller_battery(FakeOpenVr(), FakeSystem(), role=1)

        self.assertEqual(reading.percent, 73.5)
        self.assertTrue(reading.connected)
        self.assertTrue(reading.charging)

    def test_invalid_index_is_disconnected(self) -> None:
        reading = _read_controller_battery(FakeOpenVr(), FakeSystem(index=99), role=1)

        self.assertFalse(reading.connected)
        self.assertIsNone(reading.percent)

    def test_copies_the_three_by_four_openvr_matrix(self) -> None:
        class Matrix:
            m = ((1, 0, 0, 1.5), (0, 1, 0, 2.5), (0, 0, 1, 3.5))

        self.assertEqual(
            matrix_rows(Matrix()),
            ((1.0, 0.0, 0.0, 1.5), (0.0, 1.0, 0.0, 2.5), (0.0, 0.0, 1.0, 3.5)),
        )

    def test_unavailable_origin_has_no_matrix(self) -> None:
        origin = SteamVrStandingOrigin(matrix=None, available=False, detail="not ready")

        self.assertFalse(origin.available)
        self.assertIsNone(origin.matrix)

    def test_reads_a_valid_controller_position(self) -> None:
        pose = _read_controller_pose(FakeOpenVr(), FakeSystem(), [None, None, None, None, FakePose()], 1)

        self.assertTrue(pose.connected)
        self.assertTrue(pose.tracked)
        self.assertEqual(pose.position, (1.25, 0.5, -2.75))

    def test_floor_measurement_selects_lower_stable_controller(self) -> None:
        result = summarize_floor_measurement(
            {
                "left_controller": [(0.0, 0.9, 0.0), (0.0, 0.91, 0.0)],
                "right_controller": [(1.0, -0.04, 2.0), (1.0001, -0.04, 2.0)],
            }
        )

        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result.controller, "right_controller")
        self.assertTrue(result.stable)
