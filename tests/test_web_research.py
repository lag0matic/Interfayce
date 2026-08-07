import json
import socket
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import MagicMock, patch

from interfayce.web_research import (
    BraveSearchProvider, ResearchError, ResearchSession, SearchResult,
    SearxngSearchProvider, fetch_readable_page, load_brave_search_key,
    set_brave_search_key,
)
from interfayce.assistant import ConversationalAssistant
from interfayce.llm_client import LlmResponse, LlmToolCall


class _Response:
    def __init__(self, payload, *, url="https://example.com/article",
                 content_type="application/json"):
        self.payload = payload
        self.url = url
        self.headers = {"Content-Type": content_type}

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, _limit=-1):
        if isinstance(self.payload, bytes):
            return self.payload
        return json.dumps(self.payload).encode("utf-8")

    def geturl(self):
        return self.url


class _Provider:
    def search(self, query, *, freshness="any", count=5):
        return [SearchResult("EFHW Guide", "https://example.com/efhw", "A practical guide.")]


PUBLIC_DNS = [(socket.AF_INET, socket.SOCK_STREAM, 6, "", ("93.184.216.34", 443))]


class WebResearchTests(unittest.TestCase):
    def test_brave_key_is_protected_and_sent_only_as_header(self):
        import os
        with TemporaryDirectory() as directory, patch.dict(os.environ, {
            "INTERFAYCE_SECURE_DIRECTORY": directory
        }):
            set_brave_search_key("search-secret")
            self.assertEqual(load_brave_search_key(), "search-secret")
            response = _Response({"web": {"results": [{
                "title": "Title", "url": "https://example.com/page",
                "description": "Snippet",
            }]}})
            with patch("interfayce.web_research.urlopen", return_value=response) as send:
                results = BraveSearchProvider().search("antenna matching")
            request = send.call_args.args[0]
            self.assertNotIn("search-secret", request.full_url)
            self.assertEqual(request.headers["X-subscription-token"], "search-secret")
            self.assertEqual(results[0].title, "Title")

    def test_searxng_contract_is_bounded(self):
        response = _Response({"results": [{
            "title": f"Result {index}", "url": f"https://example.com/{index}",
            "content": "Snippet",
        } for index in range(8)]})
        with patch("interfayce.web_research.urlopen", return_value=response) as send:
            results = SearxngSearchProvider("http://192.168.123.45:8080").search(
                "end fed half wave antenna", freshness="year")
        self.assertEqual(len(results), 5)
        self.assertIn("format=json", send.call_args.args[0].full_url)
        self.assertIn("time_range=year", send.call_args.args[0].full_url)

    def test_session_assigns_citations_and_opens_only_stored_ids(self):
        session = ResearchSession(_Provider())
        search = session.search({"query": "EFHW antennas", "freshness": "any"})
        self.assertEqual(search["results"][0]["citation"], "[S1]")
        with self.assertRaises(ResearchError):
            session.open_result({"result_id": "S2"})
        with patch("interfayce.web_research.fetch_readable_page", return_value={
            "url": "https://example.com/efhw", "title": "EFHW Guide",
            "content": "Readable reference material about end-fed half-wave antennas.",
        }):
            opened = session.open_result({"result_id": "S1"})
        self.assertEqual(opened["citation"], "[S1]")
        self.assertIn("untrusted", opened["note"].casefold())
        self.assertEqual(session.citations()["[S1]"]["url"], "https://example.com/efhw")

    def test_page_reader_removes_active_and_navigation_content(self):
        html = b"""<html><head><title>Useful Article</title><script>steal()</script></head>
        <body><nav>Ignore navigation</nav><main><h1>EFHW Antennas</h1>
        <p>An end-fed half-wave antenna is a resonant wire antenna.</p>
        <p>Matching commonly uses a high-ratio impedance transformer.</p></main></body></html>"""
        response = _Response(html, content_type="text/html")
        opener = MagicMock()
        opener.open.return_value = response
        with patch("interfayce.web_research.socket.getaddrinfo", return_value=PUBLIC_DNS), \
                patch("interfayce.web_research.build_opener", return_value=opener):
            page = fetch_readable_page("https://example.com/article")
        self.assertEqual(page["title"], "Useful Article")
        self.assertIn("resonant wire", page["content"])
        self.assertNotIn("steal", page["content"])
        self.assertNotIn("navigation", page["content"])

    def test_private_result_is_blocked_before_opening(self):
        private_dns = [(socket.AF_INET, socket.SOCK_STREAM, 6, "", ("192.168.123.45", 443))]
        opener = MagicMock()
        with patch("interfayce.web_research.socket.getaddrinfo", return_value=private_dns), \
                patch("interfayce.web_research.build_opener", return_value=opener):
            with self.assertRaisesRegex(ResearchError, "private"):
                fetch_readable_page("https://internal.example/admin")
        opener.open.assert_not_called()

    def test_assistant_can_search_open_and_cite_in_two_tool_rounds(self):
        class Client:
            def __init__(self):
                self.responses = [
                    LlmResponse("", tool_calls=(LlmToolCall(
                        "call_search", "search_web", {"query": "EFHW antenna"}),)),
                    LlmResponse("", tool_calls=(LlmToolCall(
                        "call_open", "open_search_result", {"result_id": "S1"}),)),
                    LlmResponse("An EFHW is a resonant wire antenna [S1]."),
                ]

            def chat(self, **_request):
                return self.responses.pop(0)

        session = ResearchSession(_Provider())
        with patch("interfayce.web_research.fetch_readable_page", return_value={
            "url": "https://example.com/efhw", "title": "EFHW Guide",
            "content": "An EFHW is a resonant wire antenna with an end matching network.",
        }):
            result = ConversationalAssistant(client=Client(), tools=session.tools()).ask(
                "Research EFHW antennas."
            )
        self.assertTrue(result.succeeded)
        self.assertEqual(result.tools_used, ("search_web", "open_search_result"))
        self.assertIn("[S1]", result.response)


if __name__ == "__main__":
    unittest.main()
