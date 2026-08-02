import unittest

from interfayce.gesture import DragGesture


class DragGestureTests(unittest.TestCase):
    def test_single_press_never_starts_drag(self) -> None:
        gesture = DragGesture().pressed(10.0)

        self.assertFalse(gesture.is_dragging)

    def test_second_press_inside_window_starts_drag(self) -> None:
        gesture = DragGesture().pressed(10.0).released(10.1).pressed(10.3)

        self.assertTrue(gesture.is_dragging)

    def test_second_press_outside_window_does_not_start_drag(self) -> None:
        gesture = DragGesture().pressed(10.0).released(10.1).pressed(10.6)

        self.assertFalse(gesture.is_dragging)

    def test_release_commits_and_returns_to_idle(self) -> None:
        gesture = DragGesture().pressed(10.0).released(10.1).pressed(10.2).released(11.0)

        self.assertFalse(gesture.is_dragging)
