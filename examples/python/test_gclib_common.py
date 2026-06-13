import unittest

from gclib_common import parse_move_arg


class MoveArgTests(unittest.TestCase):
    def test_rejects_concatenated_precise_value(self):
        with self.assertRaises(ValueError):
            parse_move_arg("34.5,17,2,1not-a-number")

    def test_parses_valid_move_value(self):
        self.assertEqual(parse_move_arg("34.5,17,2,1"), (34.5, 17.0, 2, 1))


if __name__ == "__main__":
    unittest.main()
