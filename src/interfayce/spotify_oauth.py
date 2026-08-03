"""Spotify Authorization Code with PKCE and an authenticated Web API client."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import secrets
import threading
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import Request, urlopen
import webbrowser

from .secure_store import delete_secret, read_secret, write_secret
from .settings import load_settings, set_spotify_client_id


AUTHORIZE_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
API_URL = "https://api.spotify.com/v1"
CALLBACK_HOST = "127.0.0.1"
CALLBACK_PORT = 8888
CALLBACK_PATH = "/callback"
REDIRECT_URI = f"http://{CALLBACK_HOST}:{CALLBACK_PORT}{CALLBACK_PATH}"
SCOPES = (
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "playlist-read-private",
    "user-library-read",
)
_TOKEN_NAME = "spotify-oauth-token"


class SpotifyOAuthError(RuntimeError):
    pass


@dataclass(frozen=True)
class SpotifyToken:
    access_token: str
    refresh_token: str
    expires_at: float
    scope: str
    token_type: str = "Bearer"

    @property
    def needs_refresh(self) -> bool:
        return time.time() >= self.expires_at - 60.0


@dataclass(frozen=True)
class SpotifyProfile:
    user_id: str
    display_name: str
    product: str


def create_code_verifier() -> str:
    return secrets.token_urlsafe(64)


def code_challenge(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


def authorization_url(client_id: str, state: str, verifier: str) -> str:
    return AUTHORIZE_URL + "?" + urlencode({
        "response_type": "code",
        "client_id": client_id,
        "redirect_uri": REDIRECT_URI,
        "state": state,
        "scope": " ".join(SCOPES),
        "code_challenge_method": "S256",
        "code_challenge": code_challenge(verifier),
    })


def _form_request(url: str, fields: dict[str, str], *, timeout: float = 15.0) -> dict[str, Any]:
    request = Request(
        url,
        data=urlencode(fields).encode("ascii"),
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise SpotifyOAuthError(f"Spotify token request failed ({error.code}): {detail}") from error
    except URLError as error:
        raise SpotifyOAuthError(f"Spotify token service could not be reached: {error.reason}") from error


def _token_from_response(payload: dict[str, Any], previous_refresh: str = "") -> SpotifyToken:
    access_token = str(payload.get("access_token", ""))
    refresh_token = str(payload.get("refresh_token", "")) or previous_refresh
    if not access_token or not refresh_token:
        raise SpotifyOAuthError("Spotify returned an incomplete OAuth token response.")
    return SpotifyToken(
        access_token=access_token,
        refresh_token=refresh_token,
        expires_at=time.time() + int(payload.get("expires_in", 3600)),
        scope=str(payload.get("scope", "")),
        token_type=str(payload.get("token_type", "Bearer")),
    )


def save_token(token: SpotifyToken) -> None:
    write_secret(_TOKEN_NAME, json.dumps(asdict(token)).encode("utf-8"))


def load_token() -> SpotifyToken | None:
    encoded = read_secret(_TOKEN_NAME)
    if encoded is None:
        return None
    try:
        return SpotifyToken(**json.loads(encoded.decode("utf-8")))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise SpotifyOAuthError("The protected Spotify token is unreadable.") from error


def disconnect() -> bool:
    return delete_secret(_TOKEN_NAME)


def exchange_code(client_id: str, code: str, verifier: str) -> SpotifyToken:
    payload = _form_request(TOKEN_URL, {
        "client_id": client_id,
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
        "code_verifier": verifier,
    })
    token = _token_from_response(payload)
    save_token(token)
    return token


def refresh_token(client_id: str, token: SpotifyToken) -> SpotifyToken:
    payload = _form_request(TOKEN_URL, {
        "client_id": client_id,
        "grant_type": "refresh_token",
        "refresh_token": token.refresh_token,
    })
    refreshed = _token_from_response(payload, token.refresh_token)
    save_token(refreshed)
    return refreshed


def connect(client_id: str, *, timeout_seconds: float = 180.0) -> SpotifyToken:
    client_id = client_id.strip()
    if not client_id:
        raise SpotifyOAuthError("A Spotify client ID is required.")
    verifier = create_code_verifier()
    expected_state = secrets.token_urlsafe(32)
    result: dict[str, str] = {}
    completed = threading.Event()

    class CallbackHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802
            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)
            if parsed.path != CALLBACK_PATH:
                self.send_error(404)
                return
            if query.get("state", [""])[0] != expected_state:
                result["error"] = "Spotify callback state did not match."
                status, heading = 400, "Authorization rejected"
            elif error := query.get("error", [""])[0]:
                result["error"] = f"Spotify authorization failed: {error}"
                status, heading = 400, "Authorization cancelled"
            elif code := query.get("code", [""])[0]:
                result["code"] = code
                status, heading = 200, "Interfayce is connected"
            else:
                result["error"] = "Spotify callback did not contain an authorization code."
                status, heading = 400, "Authorization failed"
            body = ("<!doctype html><meta charset='utf-8'><title>Interfayce Spotify</title>"
                    f"<body style='background:#080a14;color:#e8efff;font:20px Segoe UI;padding:4rem'>"
                    f"<h1>{heading}</h1><p>You can close this window.</p></body>").encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            completed.set()

        def log_message(self, _format: str, *args: object) -> None:
            return

    try:
        server = HTTPServer((CALLBACK_HOST, CALLBACK_PORT), CallbackHandler)
    except OSError as error:
        raise SpotifyOAuthError(f"OAuth callback port {CALLBACK_PORT} is unavailable: {error}") from error
    server.timeout = 0.5
    set_spotify_client_id(client_id)
    if not webbrowser.open(authorization_url(client_id, expected_state, verifier), new=1):
        server.server_close()
        raise SpotifyOAuthError("The Spotify authorization page could not be opened.")
    deadline = time.monotonic() + timeout_seconds
    try:
        while not completed.is_set() and time.monotonic() < deadline:
            server.handle_request()
    finally:
        server.server_close()
    if not completed.is_set():
        raise SpotifyOAuthError("Spotify authorization timed out.")
    if error := result.get("error"):
        raise SpotifyOAuthError(error)
    return exchange_code(client_id, result["code"], verifier)


class SpotifyWebApi:
    def __init__(self, client_id: str | None = None) -> None:
        self.client_id = (client_id or load_settings().spotify_client_id).strip()

    def _valid_token(self) -> SpotifyToken:
        if not self.client_id:
            raise SpotifyOAuthError("Spotify is not configured with a client ID.")
        token = load_token()
        if token is None:
            raise SpotifyOAuthError("Spotify is not connected.")
        return refresh_token(self.client_id, token) if token.needs_refresh else token

    def request(self, method: str, path: str, *, query: dict[str, str | int] | None = None,
                body: dict[str, Any] | None = None) -> Any:
        token = self._valid_token()
        url = API_URL + path
        if query:
            url += "?" + urlencode(query)
        encoded = None if body is None else json.dumps(body).encode("utf-8")
        request = Request(url, data=encoded, method=method, headers={
            "Authorization": f"Bearer {token.access_token}",
            "Content-Type": "application/json",
        })
        try:
            with urlopen(request, timeout=15.0) as response:
                data = response.read()
                return None if not data else json.loads(data.decode("utf-8"))
        except HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise SpotifyOAuthError(f"Spotify Web API failed ({error.code}): {detail}") from error
        except URLError as error:
            raise SpotifyOAuthError(f"Spotify Web API could not be reached: {error.reason}") from error

    def profile(self) -> SpotifyProfile:
        data = self.request("GET", "/me")
        return SpotifyProfile(
            user_id=str(data.get("id", "")),
            display_name=str(data.get("display_name", "")) or str(data.get("id", "")),
            product=str(data.get("product", "unknown")),
        )

    def search(self, query: str, *, item_type: str = "track", limit: int = 5) -> dict[str, Any]:
        if item_type not in {"track", "album", "artist", "playlist"}:
            raise ValueError(f"Unsupported Spotify search type: {item_type}")
        return self.request("GET", "/search", query={
            "q": query, "type": item_type, "limit": max(1, min(10, limit))
        })

    def devices(self) -> list[dict[str, Any]]:
        data = self.request("GET", "/me/player/devices")
        return list(data.get("devices", []))

    def playback_state(self) -> dict[str, Any] | None:
        return self.request("GET", "/me/player")

    def start_playback(self, *, uri: str | None = None, context_uri: str | None = None,
                       uris: list[str] | None = None, device_id: str | None = None,
                       offset_uri: str | None = None) -> None:
        body: dict[str, Any] = {}
        if context_uri:
            body["context_uri"] = context_uri
            if offset_uri:
                body["offset"] = {"uri": offset_uri}
        elif uris:
            body["uris"] = uris
        elif uri:
            body["uris"] = [uri]
        query = {"device_id": device_id} if device_id else None
        self.request("PUT", "/me/player/play", query=query, body=body)

    def pause(self, *, device_id: str | None = None) -> None:
        self.request("PUT", "/me/player/pause",
                     query={"device_id": device_id} if device_id else None)

    def next(self, *, device_id: str | None = None) -> None:
        self.request("POST", "/me/player/next",
                     query={"device_id": device_id} if device_id else None)

    def previous(self, *, device_id: str | None = None) -> None:
        self.request("POST", "/me/player/previous",
                     query={"device_id": device_id} if device_id else None)

    def seek(self, position_ms: int, *, device_id: str | None = None) -> None:
        query: dict[str, str | int] = {"position_ms": max(0, position_ms)}
        if device_id:
            query["device_id"] = device_id
        self.request("PUT", "/me/player/seek", query=query)

    def set_volume(self, volume_percent: int, *, device_id: str | None = None) -> None:
        query: dict[str, str | int] = {"volume_percent": max(0, min(100, volume_percent))}
        if device_id:
            query["device_id"] = device_id
        self.request("PUT", "/me/player/volume", query=query)

    def set_shuffle(self, enabled: bool, *, device_id: str | None = None) -> None:
        query: dict[str, str | int] = {"state": str(enabled).lower()}
        if device_id:
            query["device_id"] = device_id
        self.request("PUT", "/me/player/shuffle", query=query)
