"""Local, authenticated Pear Desktop playback. No browser cookies are needed."""
from __future__ import annotations

import json
import re
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

from .media import MediaTrack
from .secure_store import read_secret, write_secret

BASE_URL = "http://127.0.0.1:26538"
TOKEN_NAME = "pear-api-token"


class PearError(RuntimeError):
    pass


def connect() -> None:
    try:
        with urlopen(Request(BASE_URL + "/auth/Interfayce", method="POST"), timeout=60) as response:
            token = json.loads(response.read(65536)).get("accessToken")
        if not isinstance(token, str) or not token:
            raise PearError("Pear did not authorize Interfayce.")
        write_secret(TOKEN_NAME, token.encode())
    except (HTTPError, URLError, OSError, ValueError) as error:
        raise PearError("Enable Pear's API Server plugin and approve Interfayce in its window.") from error


def queue_entries(data):
    result = []
    for index, item in enumerate((data or {}).get("items") or []):
        renderer = item.get("playlistPanelVideoRenderer")
        if renderer is None:
            renderer = (item.get("playlistPanelVideoWrapperRenderer", {})
                        .get("primaryRenderer", {}).get("playlistPanelVideoRenderer"))
        if isinstance(renderer, dict):
            result.append((index, renderer))
    return result


def search_tracks(data):
    """Extract only playable video IDs attached to a result's own title."""
    result, seen = [], set()
    def visit(node):
        if isinstance(node, list):
            for child in node:
                visit(child)
        elif isinstance(node, dict):
            row = node.get("musicResponsiveListItemRenderer")
            if isinstance(row, dict):
                columns = [c.get("musicResponsiveListItemFlexColumnRenderer", {})
                           .get("text", {}).get("runs", []) for c in row.get("flexColumns", [])]
                for run in columns[0] if columns else []:
                    video = run.get("navigationEndpoint", {}).get("watchEndpoint", {}).get("videoId", "")
                    if isinstance(video, str) and re.fullmatch(r"[A-Za-z0-9_-]{11}", video) and video not in seen:
                        seen.add(video)
                        result.append({"videoId": video, "title": str(run.get("text", ""))[:200],
                                       "detail": "".join(str(r.get("text", "")) for r in columns[1])[:300]
                                       if len(columns) > 1 else ""})
                return
            for value in node.values():
                visit(value)
    visit(data)
    return result[:20]


class PearApi:
    def request(self, path, method="GET", data=None):
        token = read_secret(TOKEN_NAME)
        if not token:
            raise PearError("Connect YouTube Music in Interfayce Settings first.")
        headers = {"Authorization": "Bearer " + token.decode(), "Content-Type": "application/json"}
        request = Request(BASE_URL + "/api/v1/" + path, headers=headers, method=method,
                          data=None if data is None else json.dumps(data).encode())
        try:
            with urlopen(request, timeout=5) as response:
                body = response.read(4 * 1024 * 1024 + 1)
                if len(body) > 4 * 1024 * 1024:
                    raise PearError("Pear returned too much data.")
                return json.loads(body) if body else None
        except HTTPError as error:
            raise PearError("Reconnect YouTube Music in Settings." if error.code == 401
                            else f"YouTube Music returned HTTP {error.code}.") from error
        except (URLError, OSError, ValueError) as error:
            raise PearError("YouTube Music is unavailable or did not confirm the command. It was not retried.") from error

    def song(self):
        return self.request("song") or {}

    def control(self, operation):
        routes = {"previous": "previous", "next": "next", "toggle": "toggle-play",
                  "pause": "pause", "resume": "play", "play": "play"}
        if operation not in routes:
            raise PearError("Unsupported YouTube Music control.")
        self.request(routes[operation], "POST")
        return True

    def search(self, query):
        return search_tracks(self.request("search", "POST", {"query": query}))

    def play_video(self, video_id):
        if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
            raise PearError("Invalid YouTube Music track.")
        # Reuse an existing entry; never clear the user's queue or repeat an
        # insertion when the server's dispatch acknowledgement is uncertain.
        entries = queue_entries(self.request("queue"))
        matches = [i for i, row in entries if row.get("videoId") == video_id]
        if not matches:
            self.request("queue", "POST", {"videoId": video_id, "insertPosition": "INSERT_AT_END"})
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                entries = queue_entries(self.request("queue"))
                matches = [i for i, row in entries if row.get("videoId") == video_id]
                if matches:
                    break
                time.sleep(0.2)
        if not matches:
            raise PearError("The song was sent to the queue, but Pear has not confirmed it. I haven't sent it again.")
        self.request("queue", "PATCH", {"index": matches[-1]})
        self.control("play")
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            song = self.song()
            if song.get("videoId") == video_id and song.get("isPaused") is False:
                return
            time.sleep(0.2)
        raise PearError("The playback command was sent, but Pear has not confirmed that song yet. I haven't retried it.")


class PearMedia:
    def __init__(self):
        self.api = PearApi()
        self._art_lock = threading.Lock()
        self._art_url = ""
        self._art = b""

    async def current_track_and_playback(self):
        song = self.api.song()
        title = str(song.get("title") or "").strip()
        artist = str(song.get("artist") or "").strip()
        if not title:
            return None, False
        return MediaTrack(artist=artist, title=title,
                          source_id="youtube-music:" + str(song.get("videoId") or "")), not song.get("isPaused", True)

    async def current_track(self):
        return (await self.current_track_and_playback())[0]

    async def current_art_bytes(self):
        url = self.api.song().get("imageSrc") or ""
        parsed = urlsplit(url)
        if parsed.scheme != "https" or not any((parsed.hostname or "").endswith("." + host)
            or parsed.hostname == host for host in ("ytimg.com", "googleusercontent.com", "ggpht.com")):
            return b""
        with self._art_lock:
            if url != self._art_url:
                with urlopen(url, timeout=3) as response:
                    art = response.read(2 * 1024 * 1024 + 1)
                if len(art) > 2 * 1024 * 1024:
                    return b""
                self._art_url, self._art = url, art
            return self._art

    async def play(self):
        return self.api.control("play")

    async def pause(self):
        return self.api.control("pause")

    async def toggle_play_pause(self):
        return self.api.control("toggle")

    async def next_track(self):
        return self.api.control("next")

    async def previous_track(self):
        return self.api.control("previous")
