"""Minimal OpenAI-compatible client with protected credential storage."""

from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from .secure_store import delete_secret, read_secret, write_secret
from .settings import AppSettings, load_settings


_KEY_NAME = "llm-api-key"


class LlmError(RuntimeError):
    pass


@dataclass(frozen=True)
class LlmResponse:
    content: str
    prompt_tokens: int = 0
    completion_tokens: int = 0
    estimated_cost: float | None = None


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

    tk.Label(root, text="DEEPINFRA API TOKEN", fg="#dce8ff", bg="#080b16",
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
        messagebox.showinfo("Interfayce", "The DeepInfra token is protected and saved.", parent=root)
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
        return bool(self.settings.llm_endpoint and self.settings.llm_model and load_api_key())

    def chat_json(self, *, system: str, user: str, timeout: float = 20.0) -> LlmResponse:
        api_key = load_api_key()
        if not api_key:
            raise LlmError("The LLM API key has not been configured.")
        payload: dict[str, Any] = {
            "model": self.settings.llm_model,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
            "temperature": self.settings.llm_temperature,
            "max_tokens": 220,
            "response_format": {"type": "json_object"},
        }
        if self.settings.llm_reasoning_effort:
            payload["reasoning_effort"] = self.settings.llm_reasoning_effort
        request = Request(
            self.settings.llm_endpoint + "/chat/completions",
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
            content = result["choices"][0]["message"]["content"]
            usage = result.get("usage", {})
            return LlmResponse(
                content=str(content),
                prompt_tokens=int(usage.get("prompt_tokens", 0)),
                completion_tokens=int(usage.get("completion_tokens", 0)),
                estimated_cost=float(usage["estimated_cost"])
                    if usage.get("estimated_cost") is not None else None,
            )
        except (KeyError, IndexError, TypeError, ValueError) as error:
            raise LlmError("The LLM response did not contain a chat message.") from error
