import unittest

from interfayce.osc import (
    build_chatbox_clear_packet,
    build_chatbox_input_packet,
    build_chatbox_typing_packet,
    fit_chatbox_text,
)


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

    def test_typing_packets_encode_boolean_in_the_type_tag(self) -> None:
        self.assertEqual(
            build_chatbox_typing_packet(True),
            b"/chatbox/typing\x00,T\x00\x00",
        )
        self.assertEqual(
            build_chatbox_typing_packet(False),
            b"/chatbox/typing\x00,F\x00\x00",
        )

    def test_long_text_is_normalized_and_shortened_at_a_word_boundary(self) -> None:
        result = fit_chatbox_text(
            "  This   deliberately long sentence should finish on a complete word  ",
            max_characters=40,
        )
        self.assertEqual(result, "This deliberately long sentence should…")
        self.assertLessEqual(len(result), 40)

    def test_single_long_word_uses_the_full_available_space(self) -> None:
        result = fit_chatbox_text("x" * 200)
        self.assertEqual(result, "x" * 143 + "…")
