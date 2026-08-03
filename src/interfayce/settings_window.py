"""Small native desktop settings window for session-facing controls."""

from __future__ import annotations

import socket
import tkinter as tk
from tkinter import ttk

from .settings import load_settings, set_runtime_controls


VOICE_SERVICE_PORT = 43817


def microphone_names() -> list[str]:
    try:
        import speech_recognition as sr  # type: ignore[import-not-found]

        return list(dict.fromkeys(sr.Microphone.list_microphone_names()))
    except Exception:
        return []


def voice_service_available() -> bool:
    try:
        with socket.create_connection(("127.0.0.1", VOICE_SERVICE_PORT), timeout=0.15):
            return True
    except OSError:
        return False


class SettingsWindow:
    BG = "#090d18"
    PANEL = "#101827"
    TEXT = "#e7edf8"
    MUTED = "#8490a6"
    VIOLET = "#7652e8"
    CYAN = "#26d8ec"

    def __init__(self) -> None:
        self.root = tk.Tk()
        self.root.title("Interfayce Settings")
        self.root.geometry("720x510")
        self.root.minsize(660, 480)
        self.root.configure(bg=self.BG)
        self.root.option_add("*Font", ("Segoe UI", 10))
        self._configure_style()
        current = load_settings()
        self.volume = tk.DoubleVar(value=round(current.tts_volume * 100))
        self.muted = tk.BooleanVar(value=current.tts_muted)
        self.microphone = tk.StringVar(value=current.stt_microphone)
        self.haptic = tk.DoubleVar(value=round(current.haptic_strength * 100))
        self.volume_label = tk.StringVar()
        self.haptic_label = tk.StringVar()
        self.service_status = tk.StringVar()
        self.device_status = tk.StringVar()
        self.save_status = tk.StringVar(value="Settings are shared with the wrist controls.")
        self._build()
        self.refresh_devices()
        self._update_labels()
        self._poll_status()

    def _configure_style(self) -> None:
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TFrame", background=self.BG)
        style.configure("Panel.TFrame", background=self.PANEL)
        style.configure("TLabel", background=self.BG, foreground=self.TEXT)
        style.configure("Panel.TLabel", background=self.PANEL, foreground=self.TEXT)
        style.configure("Muted.Panel.TLabel", background=self.PANEL, foreground=self.MUTED)
        style.configure("Title.TLabel", background=self.BG, foreground=self.TEXT,
                        font=("Segoe UI Semibold", 20))
        style.configure("Accent.TButton", background=self.VIOLET, foreground="white",
                        borderwidth=0, padding=(18, 10))
        style.map("Accent.TButton", background=[("active", "#8b6af0")])
        style.configure("TButton", background="#19243a", foreground=self.TEXT,
                        borderwidth=0, padding=(12, 8))
        style.configure("TCombobox", fieldbackground="#0b1220", background="#19243a",
                        foreground=self.TEXT, arrowcolor=self.CYAN, padding=8)
        style.configure("Horizontal.TScale", background=self.PANEL, troughcolor="#202c43")
        style.configure("TCheckbutton", background=self.PANEL, foreground=self.TEXT)

    def _build(self) -> None:
        outer = ttk.Frame(self.root, padding=26)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="INTERFAYCE", style="Title.TLabel").pack(anchor="w")
        ttk.Label(outer, text="Desktop configuration", foreground=self.CYAN).pack(anchor="w", pady=(0, 18))

        panel = ttk.Frame(outer, style="Panel.TFrame", padding=22)
        panel.pack(fill="both", expand=True)

        ttk.Label(panel, text="VOICE INPUT", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.device_status,
                  style="Muted.Panel.TLabel").grid(row=0, column=1, sticky="e")
        self.microphone_box = ttk.Combobox(
            panel, textvariable=self.microphone, state="readonly", width=56)
        self.microphone_box.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(7, 7))
        ttk.Button(panel, text="Refresh devices", command=self.refresh_devices).grid(
            row=2, column=0, sticky="w")

        ttk.Separator(panel).grid(row=3, column=0, columnspan=2, sticky="ew", pady=18)
        ttk.Label(panel, text="TTS OUTPUT", style="Panel.TLabel").grid(row=4, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.volume_label,
                  style="Muted.Panel.TLabel").grid(row=4, column=1, sticky="e")
        volume_scale = ttk.Scale(panel, from_=0, to=100, variable=self.volume,
                                 command=lambda _value: self._update_labels())
        volume_scale.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(8, 6))
        ttk.Checkbutton(panel, text="Mute spoken responses", variable=self.muted).grid(
            row=6, column=0, sticky="w")

        ttk.Separator(panel).grid(row=7, column=0, columnspan=2, sticky="ew", pady=18)
        ttk.Label(panel, text="HAPTIC FEEDBACK", style="Panel.TLabel").grid(row=8, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.haptic_label,
                  style="Muted.Panel.TLabel").grid(row=8, column=1, sticky="e")
        haptic_scale = ttk.Scale(panel, from_=0, to=100, variable=self.haptic,
                                 command=lambda _value: self._update_labels())
        haptic_scale.grid(row=9, column=0, columnspan=2, sticky="ew", pady=(8, 4))
        ttk.Label(panel, text="Controls Interfayce keyboard and confirmation taps.",
                  style="Muted.Panel.TLabel").grid(row=10, column=0, columnspan=2, sticky="w")

        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=1)

        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(16, 0))
        ttk.Label(footer, textvariable=self.service_status, foreground=self.MUTED).pack(side="left")
        ttk.Label(footer, textvariable=self.save_status, foreground=self.MUTED).pack(side="left", padx=20)
        ttk.Button(footer, text="Apply", style="Accent.TButton", command=self.apply).pack(side="right")

    def _update_labels(self) -> None:
        self.volume_label.set(f"{round(self.volume.get())}%")
        self.haptic_label.set(f"{round(self.haptic.get())}%")

    def refresh_devices(self) -> None:
        devices = microphone_names()
        current = self.microphone.get()
        values = ["System default"] + devices
        self.microphone_box["values"] = values
        if current and current in devices:
            self.microphone.set(current)
        elif not current or current == "System default":
            self.microphone.set("System default")
        else:
            values.append(current)
            self.microphone_box["values"] = values
            self.microphone.set(current)
        self.device_status.set(f"{len(devices)} inputs found" if devices else "No inputs found")

    def apply(self) -> None:
        selected = self.microphone.get()
        set_runtime_controls(
            tts_volume=self.volume.get() / 100.0,
            tts_muted=self.muted.get(),
            stt_microphone="" if selected == "System default" else selected,
            haptic_strength=self.haptic.get() / 100.0,
        )
        self.save_status.set("Applied")
        self.root.after(1800, lambda: self.save_status.set(
            "Settings are shared with the wrist controls."))

    def _poll_status(self) -> None:
        self.service_status.set(
            "VOICE SERVICE  •  READY" if voice_service_available()
            else "VOICE SERVICE  •  OFFLINE")
        self.root.after(1500, self._poll_status)

    def run(self) -> None:
        self.root.mainloop()


def show_settings_window() -> None:
    import ctypes

    kernel32 = ctypes.windll.kernel32
    kernel32.CreateMutexW.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    mutex = kernel32.CreateMutexW(None, False, "Local\\InterfayceSettingsWindow")
    if not mutex:
        return
    if kernel32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
        kernel32.CloseHandle(mutex)
        return
    try:
        SettingsWindow().run()
    finally:
        kernel32.CloseHandle(mutex)
