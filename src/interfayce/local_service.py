"""Authentication shared by Interfayce's same-user localhost clients."""

from __future__ import annotations

import hmac
import secrets
from typing import Mapping

from .secure_store import read_secret, write_secret


TOKEN_HEADER = "X-Interfayce-Token"
_TOKEN_NAME = "local-service-token"


def get_or_create_token() -> str:
    stored = read_secret(_TOKEN_NAME)
    if stored:
        return stored.decode("ascii")
    token = secrets.token_urlsafe(32)
    write_secret(_TOKEN_NAME, token.encode("ascii"))
    return token


def request_is_authorized(headers: Mapping[str, str], *, port: int, token: str) -> bool:
    # No browser origin is expected. Rejecting it also blocks simple cross-site
    # requests before they can reach microphone, OSC, Spotify, or shutdown routes.
    if headers.get("Origin") is not None:
        return False
    host = headers.get("Host", "").casefold()
    allowed_hosts = {"127.0.0.1", f"127.0.0.1:{port}", "localhost", f"localhost:{port}"}
    if host not in allowed_hosts:
        return False
    supplied = headers.get(TOKEN_HEADER, "")
    return bool(supplied) and hmac.compare_digest(supplied, token)
