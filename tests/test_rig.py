import unittest

from interfayce.rig import BatteryReading, DEFAULT_PERSONAL_RIG, RigSlot


class PersonalRigTests(unittest.TestCase):
    def test_foot_slot_uses_the_weaker_of_ankle_and_foot(self) -> None:
        states = DEFAULT_PERSONAL_RIG.summarize(
            {
                "left_ankle": BatteryReading(72, connected=True, runtime_seconds=12_600),
                "left_foot": BatteryReading(31, connected=True, runtime_seconds=5_400),
            }
        )
        left_foot = next(state for state in states if state.slot == RigSlot.LEFT_FOOT)

        self.assertEqual(left_foot.percent, 31)
        self.assertEqual(left_foot.runtime_seconds, 5_400)
        self.assertTrue(left_foot.connected)
        self.assertEqual(left_foot.source_count, 2)

    def test_foot_slot_warns_when_one_constituent_is_missing(self) -> None:
        states = DEFAULT_PERSONAL_RIG.summarize(
            {"right_ankle": BatteryReading(70, connected=True)}
        )
        right_foot = next(state for state in states if state.slot == RigSlot.RIGHT_FOOT)

        self.assertFalse(right_foot.connected)
        self.assertEqual(right_foot.percent, 70)

    def test_controllers_remain_independent_slots(self) -> None:
        states = DEFAULT_PERSONAL_RIG.summarize(
            {
                "left_controller": BatteryReading(90, connected=True),
                "right_controller": BatteryReading(20, connected=True),
            }
        )

        left = next(state for state in states if state.slot == RigSlot.LEFT_CONTROLLER)
        right = next(state for state in states if state.slot == RigSlot.RIGHT_CONTROLLER)
        self.assertEqual((left.percent, right.percent), (90, 20))
