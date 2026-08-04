import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from interfayce.local_service import (TOKEN_HEADER, get_or_create_token,
                                      request_is_authorized)


class LocalServiceSecurityTests(unittest.TestCase):
    def test_token_is_protected_and_stable_for_current_user(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            first = get_or_create_token()
            second = get_or_create_token()
            self.assertEqual(first, second)
            self.assertGreaterEqual(len(first), 40)
            protected = next(iter(Path(directory).glob("*.dpapi"))).read_bytes()
            self.assertNotIn(first.encode("ascii"), protected)

    def test_requests_require_token_loopback_host_and_no_browser_origin(self) -> None:
        token = "a" * 43
        valid = {"Host": "127.0.0.1:43817", TOKEN_HEADER: token}
        self.assertTrue(request_is_authorized(valid, port=43817, token=token))
        self.assertFalse(request_is_authorized(
            {"Host": "127.0.0.1:43817"}, port=43817, token=token))
        self.assertFalse(request_is_authorized(
            {**valid, "Origin": "https://example.test"}, port=43817, token=token))
        self.assertFalse(request_is_authorized(
            {**valid, "Host": "example.test"}, port=43817, token=token))


if __name__ == "__main__":
    unittest.main()
