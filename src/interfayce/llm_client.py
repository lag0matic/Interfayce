"""Minimal OpenAI-compatible client with protected credential storage."""

from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any, Mapping, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from .secure_store import delete_secret, read_secret, write_secret
from .settings import AppSettings, load_settings


_KEY_NAME = "llm-api-key"


class LlmError(RuntimeError):
    pass


def valid_llm_endpoint(endpoint: str) -> bool:
    parsed = urlparse(endpoint.strip())
    if not parsed.hostname or parsed.username or parsed.password:
        return False
    if parsed.scheme == "https":
        return True
    return parsed.scheme == "http" and parsed.hostname.casefold() in {
        "localhost", "127.0.0.1", "::1"
    }


@dataclass(frozen=True)
class LlmResponse:
    content: str
    prompt_tokens: int = 0
    completion_tokens: int = 0
    estimated_cost: float | None = None
    tool_calls: tuple["LlmToolCall", ...] = ()


@dataclass(frozen=True)
class LlmToolCall:
    call_id: str
    name: str
    arguments: dict[str, Any]


def set_api_key(api_key: str) -> None:
    cleaned = api_key.strip()
    if not cleaned:
        raise ValueError("The LLM API key cannot be empty.")
    write_secret(_KEY_NAME, cleaned.encode("utf-8"))


def load_api_key() -> str | None:
    stored = read_secret(_KEY_NAME)
    return None if stored is None else stored.decode("utf-8")


def delete_api_key() -> bool:
    return delete_secret(_KEY_NAME)


def show_api_key_dialog() -> bool:
    """Collect the key locally without exposing it through argv or stdout."""
    import tkinter as tk
    from tkinter import messagebox

    saved = False
    root = tk.Tk()
    root.title("Interfayce — LLM Connection")
    root.geometry("560x230")
    root.resizable(False, False)
    root.configure(background="#080b16")
    root.attributes("-topmost", True)

    tk.Label(root, text="LLM API TOKEN", fg="#dce8ff", bg="#080b16",
             font=("Segoe UI", 14, "bold")).pack(anchor="w", padx=28, pady=(26, 8))
    tk.Label(root, text="Stored locally using Windows account protection.",
             fg="#8090ad", bg="#080b16", font=("Segoe UI", 10)).pack(
                 anchor="w", padx=28, pady=(0, 12))
    token = tk.StringVar()
    entry = tk.Entry(root, textvariable=token, show="●", fg="#ecf4ff", bg="#11182a",
                     insertbackground="#28d9ef", relief="flat", font=("Segoe UI", 12))
    entry.pack(fill="x", padx=28, ipady=9)

    button_row = tk.Frame(root, bg="#080b16")
    button_row.pack(fill="x", padx=28, pady=22)

    def save() -> None:
        nonlocal saved
        try:
            set_api_key(token.get())
        except ValueError as error:
            messagebox.showerror("Interfayce", str(error), parent=root)
            return
        token.set("")
        saved = True
        messagebox.showinfo("Interfayce", "The LLM token is protected and saved.", parent=root)
        root.destroy()

    tk.Button(button_row, text="CANCEL", command=root.destroy, fg="#8996b2", bg="#11182a",
              activeforeground="#dce8ff", activebackground="#19233b", relief="flat",
              font=("Segoe UI", 10, "bold"), padx=24, pady=8).pack(side="right")
    tk.Button(button_row, text="SAVE", command=save, fg="#071018", bg="#28d9ef",
              activeforeground="#071018", activebackground="#6cecff", relief="flat",
              font=("Segoe UI", 10, "bold"), padx=28, pady=8).pack(side="right", padx=(0, 10))
    entry.bind("<Return>", lambda _event: save())
    entry.focus_set()
    root.after(250, lambda: root.attributes("-topmost", False))
    root.mainloop()
    return saved


class OpenAiCompatibleClient:
    def __init__(self, settings: AppSettings | None = None) -> None:
        self.settings = settings or load_settings()

    @property
    def configured(self) -> bool:
        return bool(self.settings.llm_enabled and self.settings.llm_endpoint
                    and valid_llm_endpoint(self.settings.llm_endpoint)
                    and self.settings.llm_model and load_api_key())

    def _configuration(self) -> tuple[str, str, str]:
        if not self.settings.llm_enabled:
            raise LlmError("LLM commands are disabled in Interfayce settings.")
        if not self.settings.llm_endpoint or not self.settings.llm_model:
            raise LlmError("The LLM endpoint and model have not been configured.")
        if not valid_llm_endpoint(self.settings.llm_endpoint):
            raise LlmError("The LLM endpoint must use HTTPS unless it is on this computer.")
        api_key = load_api_key()
        if not api_key:
            raise LlmError("The LLM API key has not been configured.")
        return self.settings.llm_endpoint, self.settings.llm_model, api_key

    @staticmethod
    def _messages(messages: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
        if not messages or len(messages) > 32:
            raise ValueError("LLM conversations require between 1 and 32 messages.")
        cleaned: list[dict[str, Any]] = []
        for message in messages:
            role = message.get("role")
            if role not in {"system", "user", "assistant", "tool"}:
                raise ValueError("An LLM message used an unsupported role.")
            content = message.get("content", "")
            if content is None:
                content = ""
            if not isinstance(content, str) or len(content) > 24_000:
                raise ValueError("LLM message content must be bounded text.")
            item: dict[str, Any] = {"role": role, "content": content}
            if role == "tool":
                call_id = message.get("tool_call_id")
                if not isinstance(call_id, str) or not 1 <= len(call_id) <= 160:
                    raise ValueError("A tool result requires a valid tool-call ID.")
                item["tool_call_id"] = call_id
            elif role == "assistant" and message.get("tool_calls") is not None:
                tool_calls = message["tool_calls"]
                if not isinstance(tool_calls, list) or len(tool_calls) > 4:
                    raise ValueError("An assistant message contained invalid tool calls.")
                item["tool_calls"] = tool_calls
            cleaned.append(item)
        return cleaned

    @staticmethod
    def _tools(tools: Sequence[Mapping[str, Any]] | None) -> list[dict[str, Any]] | None:
        if tools is None:
            return None
        if len(tools) > 8:
            raise ValueError("No more than eight LLM tools may be exposed at once.")
        cleaned: list[dict[str, Any]] = []
        for tool in tools:
            function = tool.get("function") if tool.get("type") == "function" else None
            if not isinstance(function, Mapping):
                raise ValueError("Only function tools are supported.")
            name = function.get("name")
            description = function.get("description")
            parameters = function.get("parameters")
            if (not isinstance(name, str) or not name.isidentifier()
                    or len(name) > 64):
                raise ValueError("An LLM tool has an invalid name.")
            if not isinstance(description, str) or len(description) > 800:
                raise ValueError("An LLM tool requires a bounded description.")
            if not isinstance(parameters, Mapping):
                raise ValueError("An LLM tool requires a JSON parameter schema.")
            cleaned.append({
                "type": "function",
                "function": {
                    "name": name,
                    "description": description,
                    "parameters": dict(parameters),
                },
            })
        return cleaned

    @staticmethod
    def _tool_calls(message: Mapping[str, Any]) -> tuple[LlmToolCall, ...]:
        raw_calls = message.get("tool_calls") or []
        if not isinstance(raw_calls, list) or len(raw_calls) > 4:
            raise LlmError("The LLM response contained invalid tool calls.")
        calls: list[LlmToolCall] = []
        for raw in raw_calls:
            function = raw.get("function") if isinstance(raw, Mapping) else None
            call_id = raw.get("id") if isinstance(raw, Mapping) else None
            name = function.get("name") if isinstance(function, Mapping) else None
            arguments = function.get("arguments") if isinstance(function, Mapping) else None
            if (not isinstance(call_id, str) or not 1 <= len(call_id) <= 160
                    or not isinstance(name, str) or not name.isidentifier()
                    or len(name) > 64 or not isinstance(arguments, str)
                    or len(arguments) > 8_000):
                raise LlmError("The LLM response contained a malformed tool call.")
            try:
                parsed = json.loads(arguments)
            except json.JSONDecodeError as error:
                raise LlmError("The LLM returned invalid tool arguments.") from error
            if not isinstance(parsed, dict):
                raise LlmError("LLM tool arguments must be a JSON object.")
            calls.append(LlmToolCall(call_id, name, parsed))
        return tuple(calls)

    def chat(self, *, messages: Sequence[Mapping[str, Any]],
             tools: Sequence[Mapping[str, Any]] | None = None,
             max_tokens: int = 500, timeout: float = 20.0,
             json_response: bool = False) -> LlmResponse:
        endpoint, model, api_key = self._configuration()
        if not 1 <= int(max_tokens) <= 2_000:
            raise ValueError("LLM output must be limited to 1 through 2000 tokens.")
        payload: dict[str, Any] = {
            "model": model,
            "messages": self._messages(messages),
            "temperature": self.settings.llm_temperature,
            "max_tokens": int(max_tokens),
        }
        clean_tools = self._tools(tools)
        if clean_tools:
            payload["tools"] = clean_tools
            payload["tool_choice"] = "auto"
        if json_response:
            payload["response_format"] = {"type": "json_object"}
        if self.settings.llm_reasoning_effort:
            payload["reasoning_effort"] = self.settings.llm_reasoning_effort
        request = Request(
            endpoint + "/chat/completions",
            data=json.dumps(payload).encode("utf-8"),
            method="POST",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
        )
        try:
            with urlopen(request, timeout=timeout) as response:
                result = json.loads(response.read().decode("utf-8"))
        except HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            raise LlmError(f"LLM request failed ({error.code}): {detail}") from error
        except URLError as error:
            raise LlmError(f"LLM endpoint could not be reached: {error.reason}") from error
        except (ValueError, json.JSONDecodeError) as error:
            raise LlmError("The LLM endpoint returned invalid JSON.") from error
        try:
            message = result["choices"][0]["message"]
            content = message.get("content") or ""
            usage = result.get("usage", {})
            return LlmResponse(
                content=str(content),
                prompt_tokens=int(usage.get("prompt_tokens", 0)),
                completion_tokens=int(usage.get("completion_tokens", 0)),
                estimated_cost=float(usage["estimated_cost"])
                    if usage.get("estimated_cost") is not None else None,
                tool_calls=self._tool_calls(message),
            )
        except (KeyError, IndexError, TypeError, ValueError) as error:
            raise LlmError("The LLM response did not contain a chat message.") from error

    def chat_json(self, *, system: str, user: str, timeout: float = 20.0) -> LlmResponse:
        return self.chat(
            messages=[
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            max_tokens=220,
            timeout=timeout,
            json_response=True,
        )
