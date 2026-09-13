"""Owned, hidden Codex app-server connection; no desktop process attachment."""
from __future__ import annotations

from concurrent.futures import Future
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading


class CodexConnectionError(RuntimeError):
    pass


def discover_codex() -> str:
    candidate = shutil.which("codex.exe") or shutil.which("codex")
    if candidate:
        return candidate
    root = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenAI/Codex/bin"
    candidates = sorted(root.glob("*/codex.exe"), key=lambda p: p.stat().st_mtime, reverse=True)
    if candidates:
        return str(candidates[0])
    raise CodexConnectionError("Install or update Codex, then try again.")


class CodexTransport:
    def __init__(self, cwd: Path, executable: str | None = None):
        self.events: queue.Queue = queue.Queue(maxsize=4096)
        self._pending: dict[int, Future] = {}
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._close_lock = threading.Lock()
        self._next_id = 0
        self._closed = False
        cwd.mkdir(parents=True, exist_ok=True)
        self.process = subprocess.Popen(
            [executable or discover_codex(), "app-server"], cwd=str(cwd),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", errors="strict",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self._reader = threading.Thread(target=self._read, name="CodexReader", daemon=True)
        self._reader.start()
        try:
            self.request("initialize", {"clientInfo": {"name": "interfayce", "version": "0.1.0"}})
            self.send({"method": "initialized", "params": {}})
        except Exception:
            self.close()
            raise

    def send(self, message: dict) -> None:
        with self._write_lock:
            if self._closed:
                raise CodexConnectionError("Codex disconnected.")
            try:
                self.process.stdin.write(json.dumps(message) + "\n")
                self.process.stdin.flush()
            except (OSError, ValueError) as error:
                raise CodexConnectionError("Codex disconnected.") from error

    @property
    def connected(self):
        return not self._closed and self.process.poll() is None

    def request(self, method: str, params: dict, timeout: float = 30) -> dict:
        with self._lock:
            self._next_id += 1
            request_id = self._next_id
            future = Future()
            self._pending[request_id] = future
        try:
            self.send({"id": request_id, "method": method, "params": params})
            return future.result(timeout=timeout)
        finally:
            with self._lock:
                self._pending.pop(request_id, None)

    def _read(self) -> None:
        try:
            for line in self.process.stdout:
                message = json.loads(line)
                if "id" in message and "method" not in message:
                    with self._lock:
                        future = self._pending.get(message["id"])
                        if future is not None and not future.done():
                            if "error" in message:
                                future.set_exception(CodexConnectionError(str(message["error"])))
                            else:
                                future.set_result(message.get("result", {}))
                else:
                    self.events.put_nowait(message)
        except (ValueError, OSError, queue.Full):
            pass
        finally:
            self._closed = True
            with self._lock:
                for future in self._pending.values():
                    if not future.done():
                        future.set_exception(CodexConnectionError("Codex disconnected; request was not replayed."))

    def close(self) -> None:
        with self._close_lock:
            self._close_owned_process()

    def _close_owned_process(self) -> None:
        self._closed = True
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self._reader.join(timeout=2)
        for stream in (self.process.stdin, self.process.stdout):
            if stream:
                stream.close()
