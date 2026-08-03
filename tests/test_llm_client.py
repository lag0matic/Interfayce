import json
import os
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import MagicMock, patch

from interfayce.llm_client import LlmError, OpenAiCompatibleClient, load_api_key, set_api_key
from interfayce.settings import AppSettings


class _Response:
    def __init__(self, payload: dict) -> None:
        self.payload = payload

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self) -> bytes:
        return json.dumps(self.payload).encode("utf-8")


class LlmClientTests(unittest.TestCase):
    def test_key_is_protected_and_request_is_constrained_to_json(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            set_api_key("secret-token")
            self.assertEqual(load_api_key(), "secret-token")
            response = _Response({
                "choices": [{"message": {"content": '{"action":"none"}'}}],
                "usage": {"prompt_tokens": 10, "completion_tokens": 4, "estimated_cost": 0.00001},
            })
            with patch("interfayce.llm_client.urlopen", return_value=response) as send:
                result = OpenAiCompatibleClient(AppSettings(
                    llm_enabled=True,
                    llm_endpoint="https://example.test/v1",
                    llm_model="example-model",
                )).chat_json(
                    system="system", user="user"
                )
            request = send.call_args.args[0]
            body = json.loads(request.data)
            self.assertEqual(body["response_format"], {"type": "json_object"})
            self.assertEqual(body["max_tokens"], 220)
            self.assertNotIn("secret-token", request.data.decode("utf-8"))
            self.assertEqual(result.prompt_tokens, 10)

    def test_missing_key_fails_before_network(self) -> None:
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            with self.assertRaises(LlmError):
                OpenAiCompatibleClient(AppSettings(
                    llm_enabled=True,
                    llm_endpoint="https://example.test/v1",
                    llm_model="example-model",
                )).chat_json(system="s", user="u")

    def test_disabled_llm_never_reads_credentials_or_touches_network(self) -> None:
        client = OpenAiCompatibleClient(AppSettings(
            llm_enabled=False,
            llm_endpoint="https://example.test/v1",
            llm_model="example-model",
        ))
        with patch("interfayce.llm_client.load_api_key") as read_key, \
                patch("interfayce.llm_client.urlopen") as send:
            self.assertFalse(client.configured)
            with self.assertRaisesRegex(LlmError, "disabled"):
                client.chat_json(system="s", user="u")
        read_key.assert_not_called()
        send.assert_not_called()


if __name__ == "__main__":
    unittest.main()
