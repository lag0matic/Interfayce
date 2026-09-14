"""Bounded, cached availability checks; never generate speech or LLM tokens."""
from __future__ import annotations

import json
import os
import threading
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from .settings import load_settings


def check_json(url, headers=None):
    with urlopen(Request(url, headers=headers or {}), timeout=3) as response:
        return json.loads(response.read(262144))


class ServiceHealth:
    def __init__(self, transcriber):
        self.transcriber = transcriber
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._values = {}
        self._local_ready = False
        self.assistant_health = None

    def set(self, name, state, detail):
        with self._lock:
            self._values[name] = (state, detail, time.monotonic())

    def wire(self):
        with self._lock:
            rows = []
            for name in ("TTS", "STT", "LLM", "SPOTIFY"):
                state, detail, stamp = self._values.get(name, ("unknown", "Checking", 0))
                if time.monotonic() - stamp > 45:
                    state, detail = "unknown", "Waiting for a fresh check"
                rows.append("\t".join((name, state, detail.replace("\t", " ").replace("\n", " "))))
            return "\n".join(rows)

    def check(self, name):
        s = load_settings()
        if name == "TTS":
            endpoint = os.environ.get("INTERFAYCE_KOKORO_URL", s.tts_endpoint).strip()
            if s.tts_muted or s.tts_volume <= 0:
                return "unknown", "Muted"
            if not endpoint:
                return "unknown", "Not configured"
            base = endpoint.removesuffix("/audio/speech").rstrip("/")
            check_json(base + "/models")
            return "good", "Speech server reachable"
        if name == "LLM":
            if self.assistant_health is not None:
                selected = self.assistant_health()
                if selected is not None:
                    return selected
            from .llm_client import OpenAiCompatibleClient, load_api_key
            if not OpenAiCompatibleClient().configured:
                return "unknown", "Disabled or not configured"
            check_json(s.llm_endpoint.rstrip("/") + "/models",
                       {"Authorization": "Bearer " + load_api_key()})
            return "good", "API reachable; authentication accepted"
        if name == "STT":
            from .remote_stt import RemoteSttTranscriber
            provider = (self.transcriber.current_provider() if hasattr(self.transcriber, "current_provider") else self.transcriber)
            remote = isinstance(provider, RemoteSttTranscriber)
            if remote:
                try:
                    check_json(provider.health_endpoint, provider._headers())
                    return "good", "Remote speech recognition reachable"
                except Exception:
                    if provider.fallback is None:
                        return "offline", "Remote unavailable; no local fallback"
            if not self._local_ready:
                local = provider.fallback if remote else provider
                local.warm()
                self._local_ready = True
            return ("backup", "Remote unavailable; local recognition ready") if remote else ("good", "Local recognition ready")
        return "unknown", "Waiting for media session"

    def _run(self, name):
        while not self._stop.is_set():
            try:
                state, detail = self.check(name)
            except HTTPError as error:
                state, detail = (("unknown", "Health check unsupported") if error.code in (404, 405)
                                 else ("offline", f"Service returned HTTP {error.code}"))
            except Exception:
                state, detail = "offline", "Service unreachable or not ready"
            self.set(name, state, detail)
            self._stop.wait(15)

    def start(self):
        for name in ("TTS", "STT", "LLM"):
            threading.Thread(target=self._run, args=(name,), daemon=True,
                             name="Health-" + name).start()

    def stop(self):
        self._stop.set()
