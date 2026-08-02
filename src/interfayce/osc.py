"""Minimal OSC output for VRChat's local chatbox endpoint."""

from __future__ import annotations

from dataclasses import dataclass
import socket

DEFAULT_VRCHAT_HOST = "127.0.0.1"
DEFAULT_VRCHAT_PORT = 9000
CHATBOX_INPUT_ADDRESS = "/chatbox/input"


def _osc_string(value: str) -> bytes:
    """Encode and four-byte-pad an OSC string."""
    encoded = value.encode("utf-8") + b"\x00"
    padding = (-len(encoded)) % 4
    return encoded + (b"\x00" * padding)


def _build_chatbox_packet(text: str, *, send_immediately: bool = True) -> bytes:
    """Build VRChat's `/chatbox/input` packet without a third-party OSC library.

    The final `False` disables the chatbox notification sound. VRChat represents
    booleans in the OSC type tag, so they do not have separate payload bytes.
    """
    if len(text) > 144:
        raise ValueError("VRChat chatbox text cannot exceed 144 characters.")

    immediate_tag = "T" if send_immediately else "F"
    return b"".join(
        (
            _osc_string(CHATBOX_INPUT_ADDRESS),
            _osc_string(f",s{immediate_tag}F"),
            _osc_string(text),
        )
    )


def build_chatbox_input_packet(text: str, *, send_immediately: bool = True) -> bytes:
    """Build a normal non-empty VRChat chatbox message."""
    if not text:
        raise ValueError("Chatbox text cannot be empty.")
    return _build_chatbox_packet(text, send_immediately=send_immediately)


def build_chatbox_clear_packet() -> bytes:
    """Build the undocumented empty-input workaround for clearing a chatbox.

    This is intentionally separate from normal message sending because VRChat
    does not officially document empty OSC chatbox input as a clear operation.
    """
    return _build_chatbox_packet("")


@dataclass(frozen=True)
class VrchatOscClient:
    """Sends only the local OSC actions Interfayce explicitly owns."""

    host: str = DEFAULT_VRCHAT_HOST
    port: int = DEFAULT_VRCHAT_PORT

    def send_chatbox_message(self, text: str) -> None:
        packet = build_chatbox_input_packet(text)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.sendto(packet, (self.host, self.port))

    def clear_chatbox(self) -> None:
        """Try VRChat's undocumented empty-message chatbox clear behavior."""
        packet = build_chatbox_clear_packet()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.sendto(packet, (self.host, self.port))
