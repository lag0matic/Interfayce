"""Backend selection and versioned wrist snapshots."""
from __future__ import annotations

import json
import threading

from .codex_backend import CodexBackend


class AssistantSession:
    def __init__(self, legacy):
        self.legacy = legacy
        self.codex = CodexBackend()
        self._selection = self.codex.directory / 'backend.json'
        self.backend = 'codex'
        try:
            saved = json.loads(self._selection.read_text(encoding='utf-8'))
            if saved in ('codex', 'legacy'):
                self.backend = saved
        except (OSError, ValueError):
            pass
        self.cancelled = threading.Event()
        self.generation = 0

    def select(self):
        if self.codex.snapshot()['active']:
            raise RuntimeError('Stop the current answer before switching assistants.')
        self.cancel()
        self.backend = 'legacy' if self.backend == 'codex' else 'codex'
        temporary = self._selection.with_suffix('.tmp')
        temporary.write_text(json.dumps(self.backend), encoding='utf-8')
        temporary.replace(self._selection)

    def cancel(self):
        self.generation += 1
        self.cancelled.set()
        self.codex.cancel()
        self.legacy.assistant.cancel()

    def clear(self):
        self.cancel()
        if self.backend == 'codex':
            self.codex.new_conversation()
        else:
            self.legacy.assistant.clear()

    def close(self):
        self.cancel()
        self.codex.close()
