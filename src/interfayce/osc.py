"""Minimal OSC output for VRChat's local chatbox endpoint."""

from __future__ import annotations

from dataclasses import dataclass
import socket

DEFAULT_VRCHAT_HOST = "127.0.0.1"
DEFAULT_VRCHAT_PORT = 9000
CHATBOX_INPUT_ADDRESS = "/chatbox/input"
CHATBOX_TYPING_ADDRESS = "/chatbox/typing"
MAX_CHATBOX_CHARACTERS = 144


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
    if len(text) > MAX_CHATBOX_CHARACTERS:
        raise ValueError(
            f"VRChat chatbox text cannot exceed {MAX_CHATBOX_CHARACTERS} characters.")

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


def build_chatbox_typing_packet(is_typing: bool) -> bytes:
    """Build VRChat's boolean `/chatbox/typing` presence packet."""
    return b"".join((
        _osc_string(CHATBOX_TYPING_ADDRESS),
        _osc_string(",T" if is_typing else ",F"),
    ))


def fit_chatbox_text(text: object, *, max_characters: int = MAX_CHATBOX_CHARACTERS) -> str:
    """Normalize and shorten chatbox text without cutting through a normal word."""
    normalized = " ".join(str(text).split())
    if max_characters < 1:
        return ""
    if len(normalized) <= max_characters:
        return normalized
    if max_characters == 1:
        return "…"

    raw_prefix = normalized[:max_characters - 1]
    prefix = raw_prefix.rstrip()
    ended_on_boundary = (bool(raw_prefix) and raw_prefix[-1].isspace()) or (
        len(normalized) > len(raw_prefix) and normalized[len(raw_prefix)].isspace())
    boundary = prefix.rfind(" ")
    # Do not throw away most of the available chatbox for one unusually long
    # word. In that case a hard glyph boundary plus ellipsis is the least-bad fit.
    if not ended_on_boundary and boundary >= max_characters // 2:
        prefix = prefix[:boundary].rstrip()
    return prefix + "…"


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

    def set_typing(self, is_typing: bool) -> None:
        packet = build_chatbox_typing_packet(is_typing)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.sendto(packet, (self.host, self.port))
