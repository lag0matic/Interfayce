import unittest
from unittest.mock import MagicMock, patch

from interfayce.__main__ import main


class CommandTests(unittest.TestCase):
    def test_chatbox_test_sends_the_expected_message(self) -> None:
        with patch("sys.argv", ["interfayce", "chatbox-test"]):
            with patch("interfayce.__main__.VrchatOscClient") as client_type:
                main()

        client_type.return_value.send_chatbox_message.assert_called_once_with("♫ Test — Interfayce")

    def test_assistant_command_runs_outside_vr(self) -> None:
        result = MagicMock()
        with patch("sys.argv", ["interfayce", "assistant", "Hello there"]), \
                patch("interfayce.assistant_harness.AssistantHarness") as harness_type, \
                patch("interfayce.assistant_harness.print_harness_result") as output:
            harness_type.return_value.ask.return_value = result
            main()
        harness_type.return_value.ask.assert_called_once_with("Hello there", speak=False)
        output.assert_called_once_with(result)
