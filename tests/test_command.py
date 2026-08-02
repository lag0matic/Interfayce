import unittest
from unittest.mock import patch

from interfayce.__main__ import main


class CommandTests(unittest.TestCase):
    def test_chatbox_test_sends_the_expected_message(self) -> None:
        with patch("sys.argv", ["interfayce", "chatbox-test"]):
            with patch("interfayce.__main__.VrchatOscClient") as client_type:
                main()

        client_type.return_value.send_chatbox_message.assert_called_once_with("♫ Test — Interfayce")

