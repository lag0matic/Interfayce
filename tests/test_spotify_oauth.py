import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

from interfayce.secure_store import protect, read_secret, unprotect, write_secret
from interfayce.spotify_oauth import (
    REDIRECT_URI,
    SpotifyToken,
    SpotifyWebApi,
    _token_from_response,
    authorization_url,
    code_challenge,
    load_token,
    save_token,
)


class SecureStoreTests(unittest.TestCase):
    def test_dpapi_round_trip(self) -> None:
        protected = protect(b"refresh-token-test")
        self.assertNotIn(b"refresh-token-test", protected)
        self.assertEqual(unprotect(protected), b"refresh-token-test")

    def test_named_secret_round_trip(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            write_secret("sample", b"private")
            self.assertEqual(read_secret("sample"), b"private")
            self.assertNotIn(b"private", (Path(directory) / "sample.dpapi").read_bytes())


class SpotifyOAuthTests(unittest.TestCase):
    def test_pkce_challenge_matches_rfc_example(self) -> None:
        verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"
        self.assertEqual(code_challenge(verifier), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM")

    def test_authorization_url_uses_loopback_pkce_and_state(self) -> None:
        parsed = urlparse(authorization_url("client", "state-value", "verifier"))
        query = parse_qs(parsed.query)
        self.assertEqual(query["client_id"], ["client"])
        self.assertEqual(query["state"], ["state-value"])
        self.assertEqual(query["redirect_uri"], [REDIRECT_URI])
        self.assertEqual(query["code_challenge_method"], ["S256"])

    def test_token_is_dpapi_protected_and_loadable(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            token = SpotifyToken("access", "refresh", time.time() + 3600, "scope")
            save_token(token)
            raw = (Path(directory) / "spotify-oauth-token.dpapi").read_bytes()
            self.assertNotIn(b"access", raw)
            self.assertEqual(load_token(), token)

    def test_refresh_response_keeps_existing_refresh_token(self) -> None:
        with patch("interfayce.spotify_oauth.time.time", return_value=1000):
            token = _token_from_response({
                "access_token": "new-access",
                "expires_in": 3600,
                "scope": "scope",
            }, previous_refresh="existing-refresh")
        self.assertEqual(token.refresh_token, "existing-refresh")
        self.assertEqual(token.expires_at, 4600)

    def test_search_rejects_unknown_type_before_network(self) -> None:
        with self.assertRaises(ValueError):
            SpotifyWebApi("client").search("query", item_type="podcast")


if __name__ == "__main__":
    unittest.main()
