"""File persistence for settings, independent of settings fields and UI."""
from __future__ import annotations

from contextlib import contextmanager
import functools
import json
import os
from pathlib import Path
import threading
import uuid
from typing import Callable


class SettingsStore:
    def __init__(self, path: Callable[[], Path]):
        self.path = path
        self.lock = threading.RLock()
        self._transaction = threading.local()

    @contextmanager
    def transaction(self):
        """Serialize read/modify/write across threads and resident processes."""
        with self.lock:
            if getattr(self._transaction, "active", False):
                yield
                return
            lock_path = self.path().with_suffix(".lock")
            lock_path.parent.mkdir(parents=True, exist_ok=True)
            with lock_path.open("a+b") as handle:
                if handle.tell() == 0:
                    handle.write(b"0")
                    handle.flush()
                handle.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
                else:
                    import fcntl
                    fcntl.flock(handle, fcntl.LOCK_EX)
                self._transaction.active = True
                try:
                    yield
                finally:
                    self._transaction.active = False
                    handle.seek(0)
                    if os.name == "nt":
                        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                    else:
                        fcntl.flock(handle, fcntl.LOCK_UN)

    def transactional(self, function):
        @functools.wraps(function)
        def wrapped(*args, **kwargs):
            with self.transaction():
                return function(*args, **kwargs)
        return wrapped

    @staticmethod
    def write_json(path: Path, value: object):
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + "." + uuid.uuid4().hex + ".tmp")
        try:
            temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
