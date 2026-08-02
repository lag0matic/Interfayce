import unittest

from interfayce.osc import build_chatbox_clear_packet, build_chatbox_input_packet


class ChatboxPacketTests(unittest.TestCase):
    def test_packet_contains_address_types_and_message(self) -> None:
        packet = build_chatbox_input_packet("♫ Ghost — Witch Image")

        self.assertIn(b"/chatbox/input\x00", packet)
        self.assertIn(b",sTF\x00", packet)
        self.assertIn("♫ Ghost — Witch Image".encode("utf-8") + b"\x00", packet)

    def test_empty_message_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            build_chatbox_input_packet("")

    def test_clear_packet_has_an_empty_string_payload(self) -> None:
        packet = build_chatbox_clear_packet()

        self.assertIn(b"/chatbox/input\x00", packet)
        self.assertIn(b",sTF\x00", packet)
        self.assertTrue(packet.endswith(b"\x00\x00\x00\x00"))

    def test_too_long_message_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            build_chatbox_input_packet("x" * 145)
