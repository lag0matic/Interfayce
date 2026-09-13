"""Persistent subscription assistant, independent of the desktop app's process."""
from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import threading
import time
import uuid

from .codex_transport import CodexTransport, CodexConnectionError
from .settings import load_settings
from .codex_items import CodexItems


class CodexBackend:
    def __init__(self, directory: Path | None = None, transport_factory=CodexTransport):
        self.directory = directory or Path(os.environ.get("LOCALAPPDATA", Path.home())) / "Interfayce/assistant"
        self.directory.mkdir(parents=True, exist_ok=True)
        self._factory = transport_factory
        self._transport = None
        self._connecting_transport = None
        self._connection_generation = 0
        self._lock = threading.RLock()
        self._busy = threading.Lock()
        self._cancelled = threading.Event()
        self.thread_id = None
        self.turn_id = None
        self.state = "READY"
        self.response = ""
        self.transcript = ""
        self.history = ""
        self.pending = None
        self.card = None
        self._answers = {}
        self._question_index = 0
        self._reference = self.directory / "conversation.json"
        if self._reference.exists():
            try:
                self.thread_id = json.loads(self._reference.read_text(encoding="utf-8"))["threadId"]
            except (OSError, ValueError, KeyError):
                self.state = "Saved conversation could not be read; start a new conversation."

    def snapshot(self):
        with self._lock:
            return {"version": 1, "backend": "codex", "status": self.state,
                    "transcript": self.transcript, "response": self.history + self.response,
                    "active": self._busy.locked(), "pending": self.card}

    def _connect(self):
        with self._lock:
            generation = self._connection_generation
            existing = self._transport
            if existing is not None and getattr(existing, 'connected', existing.process.poll() is None):
                return existing
            self._transport = None
        if existing is not None:
            existing.close()
        transport = self._factory(self.directory)
        try:
            with self._lock:
                if generation != self._connection_generation:
                    raise CodexConnectionError('Connection cancelled during startup.')
                self._connecting_transport = transport
            account = transport.request("account/read", {}).get("account") or {}
            if account.get("type") != "chatgpt":
                raise CodexConnectionError("Sign in to Codex with ChatGPT before using ASK.")
            params = {"cwd": str(self.directory), "sandbox": "read-only",
                      "approvalPolicy": "on-request", "model": load_settings().codex_model,
                      "serviceTier": "default"}
            if self.thread_id:
                params["threadId"] = self.thread_id
                result = transport.request("thread/resume", params)
                entries = []
                for turn in result['thread'].get('turns', [])[-12:]:
                    for item in turn.get('items', []):
                        if item.get('type') == 'userMessage':
                            text = ' '.join(c.get('text', '') for c in item.get('content', []) if c.get('type') == 'text')
                            entries.append('YOU: ' + text)
                        elif item.get('type') == 'agentMessage' and item.get('text'):
                            entries.append(item['text'])
                self.history = ('\n\n'.join(entries) + '\n\n')[-16000:] if entries else ''
            else:
                params["developerInstructions"] = (
                    "You are a conversational assistant in Interfayce, a VR wrist panel. "
                    "Use concise natural replies unless detail is requested. "
                    "The user is in VR; avoid unnecessary formatting. "
                    "This is not a coding project. Do not inspect files unless asked.")
                result = transport.request("thread/start", params)
            self.thread_id = result["thread"]["id"]
            temporary = self._reference.with_suffix(".tmp")
            temporary.write_text(json.dumps({"threadId": self.thread_id}), encoding="utf-8")
            temporary.replace(self._reference)
            with self._lock:
                if generation != self._connection_generation:
                    raise CodexConnectionError('Connection cancelled during startup.')
                self._transport = transport
            return transport
        except Exception:
            transport.close()
            raise
        finally:
            with self._lock:
                if self._connecting_transport is transport:
                    self._connecting_transport = None

    def ask(self, text: str, on_update=lambda: None, *, cancel=None):
        if not self._busy.acquire(blocking=False):
            raise RuntimeError("An assistant turn is already active.")
        self._cancelled = cancel if cancel is not None else threading.Event()
        with self._lock:
            if self.response and self.state == 'ANSWER':
                self.history = (self.history + 'YOU: ' + self.transcript + '\n\n' + self.response + '\n\n')[-16000:]
            self.transcript, self.response, self.state = text, "", "CONNECTING"
        try:
            if self._cancelled.is_set():
                self.state = 'CANCELLED'
                return ''
            transport = self._connect()
            if self._cancelled.is_set():
                self.state = "CANCELLED"
                return ""
            result = transport.request("turn/start", {"threadId": self.thread_id, "effort": "low",
                "model": load_settings().codex_model, "serviceTier": "default",
                "input": [{"type": "text", "text": text}]})
            self.turn_id = result["turn"]["id"]
            self.state = "THINKING"
            interrupted = False
            items = CodexItems()
            deadline = time.monotonic() + 600
            while time.monotonic() < deadline:
                if self._cancelled.is_set() and not interrupted:
                    transport.request("turn/interrupt", {"threadId": self.thread_id, "turnId": self.turn_id})
                    interrupted = True
                try:
                    event = transport.events.get(timeout=0.2)
                except queue.Empty:
                    if not getattr(transport, 'connected', transport.process.poll() is None):
                        raise CodexConnectionError("Codex disconnected. Your prompt was not resent.")
                    continue
                method, params = event.get("method"), event.get("params", {})
                if params.get("threadId", self.thread_id) != self.thread_id:
                    continue
                if params.get('turnId', self.turn_id) != self.turn_id:
                    continue
                items.observe(method, params)
                if "id" in event:
                    with self._lock:
                        if self.pending is not None:
                            raise CodexConnectionError("Multiple simultaneous decisions are not supported. Please stop and retry in the desktop app.")
                        self.pending = event
                        self.card = {"token": uuid.uuid4().hex, "text": json.dumps(params, ensure_ascii=False),
                                     "choices": ["Stop"]}
                        if method in ("item/commandExecution/requestApproval", "item/fileChange/requestApproval"):
                            self.card["choices"] = ["Allow once", "Decline"]
                            self.card["text"] = str(params.get("reason") or "Approval requested") + "\n" + str(params.get("command") or params.get("grantRoot") or json.dumps(params, ensure_ascii=False))
                            if method == 'item/fileChange/requestApproval':
                                changes = items.file_approval(params.get('itemId'))
                                if changes is None:
                                    self.card['choices'] = ['Stop']
                                    self.card['text'] = 'The complete file change is unavailable or too large for this panel. Stop and review it in the desktop app.'
                                else:
                                    self.card['text'] = str(params.get('reason') or 'Review this file change:') + '\n\n' + changes
                        elif method == "item/tool/requestUserInput":
                            self._answers = {}
                            self._question_index = 0
                            self._question_card()
                        else:
                            self.card["text"] = "This request needs the full desktop interface. Stop this turn and continue there.\n" + str(method)
                        self.state = "AWAITING INPUT"
                elif method == "serverRequest/resolved":
                    with self._lock:
                        if self.pending and self.pending["id"] == params.get("requestId"):
                            self.pending = self.card = None
                elif method in ('item/agentMessage/delta', 'item/started', 'item/completed'):
                    with self._lock:
                        self.response = items.display()
                        if self.pending is None:
                            self.state = 'RESPONDING' if method == 'item/agentMessage/delta' else 'THINKING'
                elif method == "turn/completed" and params["turn"]["id"] == self.turn_id:
                    turn = params["turn"]
                    if turn["status"] == "failed":
                        raise CodexConnectionError(str(turn.get("error") or "Codex turn failed."))
                    self.state = "CANCELLED" if interrupted or turn["status"] == "interrupted" else "ANSWER"
                    return "" if self.state == "CANCELLED" else items.speech()
                on_update()
            raise CodexConnectionError("Codex response timed out. Your prompt was not resent.")
        except Exception as error:
            self.state = "ERROR"
            if self._transport:
                self._transport.close()
                self._transport = None
            raise CodexConnectionError(str(error)) from error
        finally:
            with self._lock:
                self.pending = None
                self.card = None
                self.turn_id = None
            self._busy.release()
            on_update()

    def cancel(self):
        self._cancelled.set()

    def new_conversation(self):
        if self._busy.locked():
            raise RuntimeError("Stop the current answer before starting a new conversation.")
        self.close()
        self._reference.unlink(missing_ok=True)
        self.thread_id = None
        self.response = self.transcript = ""
        self.history = ""
        self.state = "READY"

    def respond(self, request_id, result):
        with self._lock:
            if not self.pending or self.pending["id"] != request_id or self._cancelled.is_set():
                raise ValueError("This request is no longer active.")
            self._transport.send({"id": request_id, "result": result})
            self.pending = None
            self.card = None
            self.state = "THINKING"

    def choose(self, token, choice):
        with self._lock:
            if not self.card or self.card['token'] != token:
                raise ValueError('This decision is no longer active.')
            if self.card['choices'] == ['Stop']:
                self.cancel()
                return
            if self.pending['method'] == 'item/tool/requestUserInput':
                if choice < 0 or choice >= len(self.card['choices']):
                    raise ValueError('Invalid choice.')
                self.answer_question(self.card['choices'][choice])
                return
            if choice not in (0, 1):
                raise ValueError('Invalid choice.')
            self.respond(self.pending['id'], {'decision': 'accept' if choice == 0 else 'decline'})

    def _question_card(self):
        questions = self.pending['params'].get('questions', [])
        if self._question_index >= len(questions):
            self.respond(self.pending['id'], {'answers': self._answers})
            return
        question = questions[self._question_index]
        options = question.get('options') or []
        self.card = {'token': uuid.uuid4().hex,
                     'text': question.get('question', '') + '\n' + '\n'.join(
                         o['label'] + ': ' + o.get('description', '') for o in options),
                     'canDictate': True,
                     'choices': [o['label'] for o in options[:3]] if options else ['Stop']}
        if not options:
            self.card['text'] += '\nTap ANSWER to dictate a reply.'

    def dictate_answer(self, token, answer):
        with self._lock:
            if not self.card or self.card['token'] != token or not self.card.get('canDictate') or self._cancelled.is_set():
                raise ValueError('This question is no longer active.')
            self.answer_question(answer)

    def answer_question(self, answer):
        question = self.pending['params']['questions'][self._question_index]
        self._answers[question['id']] = {'answers': [answer]}
        self._question_index += 1
        self._question_card()

    def health(self):
        if self.state == 'ERROR':
            return 'offline', 'Codex connection failed; open ASK for details'
        if self._transport is None:
            return 'unknown', 'Codex will connect when you ask'
        if self._transport.process.poll() is not None:
            return 'offline', 'Codex disconnected'
        return 'good', 'Codex connected with ChatGPT subscription'

    def restore(self):
        if not self.thread_id or not self._busy.acquire(blocking=False):
            return
        try:
            self._connect()
        finally:
            self._busy.release()

    def close(self):
        self.cancel()
        with self._lock:
            self._connection_generation += 1
            connections = (self._transport, self._connecting_transport)
            self._transport = None
            self._connecting_transport = None
        for connection in connections:
            if connection is not None:
                connection.close()
