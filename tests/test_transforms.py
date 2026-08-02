import unittest

from interfayce.transforms import translated_origin, translation_delta


class TransformTests(unittest.TestCase):
    def test_controller_delta_is_current_minus_start(self) -> None:
        self.assertEqual(translation_delta((1.0, 2.0, 3.0), (1.5, 1.75, 4.0)), (0.5, -0.25, 1.0))

    def test_translation_only_changes_the_last_column(self) -> None:
        baseline = ((1.0, 0.0, 0.0, 10.0), (0.0, 1.0, 0.0, 20.0), (0.0, 0.0, 1.0, 30.0))

        self.assertEqual(
            translated_origin(baseline, (0.5, -1.5, 2.0)),
            ((1.0, 0.0, 0.0, 10.5), (0.0, 1.0, 0.0, 18.5), (0.0, 0.0, 1.0, 32.0)),
        )

    def test_invalid_matrix_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            translated_origin(((1.0, 0.0),), (0.0, 0.0, 0.0))
