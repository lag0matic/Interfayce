"""Native desktop configuration for Interfayce and its integrations."""

from __future__ import annotations

import socket
import threading
import tkinter as tk
from tkinter import messagebox, ttk
from urllib.parse import urlparse

from .llm_client import delete_api_key, load_api_key, set_api_key
from .settings import load_settings, set_desktop_configuration
from .spotify_oauth import (SpotifyOAuthError, SpotifyWebApi, connect as connect_spotify,
                            disconnect as disconnect_spotify, load_token)


VOICE_SERVICE_PORT = 43817


def microphone_names() -> list[str]:
    try:
        import speech_recognition as sr  # type: ignore[import-not-found]

        return list(dict.fromkeys(sr.Microphone.list_microphone_names()))
    except Exception:
        return []


def output_device_names() -> list[str]:
    try:
        import pyaudio  # type: ignore[import-not-found]

        audio = pyaudio.PyAudio()
        try:
            return list(dict.fromkeys(
                str(audio.get_device_info_by_index(index).get("name", ""))
                for index in range(audio.get_device_count())
                if audio.get_device_info_by_index(index).get("maxOutputChannels", 0) > 0
            ))
        finally:
            audio.terminate()
    except Exception:
        return []


def voice_service_available() -> bool:
    try:
        with socket.create_connection(("127.0.0.1", VOICE_SERVICE_PORT), timeout=0.15):
            return True
    except OSError:
        return False


def _valid_http_url(value: str) -> bool:
    parsed = urlparse(value.strip())
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


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
        self.root.geometry("820x720")
        self.root.minsize(760, 650)
        self.root.configure(bg=self.BG)
        self.root.option_add("*Font", ("Segoe UI", 10))
        self._configure_style()

        current = load_settings()
        self.volume = tk.DoubleVar(value=round(current.tts_volume * 100))
        self.muted = tk.BooleanVar(value=current.tts_muted)
        self.speed = tk.DoubleVar(value=current.tts_speed)
        self.microphone = tk.StringVar(value=current.stt_microphone)
        self.tts_output = tk.StringVar(value=current.tts_output)
        self.haptic = tk.DoubleVar(value=round(current.haptic_strength * 100))
        self.broadcast_gain = tk.DoubleVar(value=current.broadcast_gain_db)
        self.tts_endpoint = tk.StringVar(value=current.tts_endpoint)
        self.tts_model = tk.StringVar(value=current.tts_model)
        self.tts_voice = tk.StringVar(value=current.tts_voice)
        self.spotify_client_id = tk.StringVar(value=current.spotify_client_id)
        self.llm_enabled = tk.BooleanVar(value=current.llm_enabled)
        self.llm_endpoint = tk.StringVar(value=current.llm_endpoint)
        self.llm_model = tk.StringVar(value=current.llm_model)
        self.llm_reasoning = tk.StringVar(value=current.llm_reasoning_effort)
        self.llm_temperature = tk.DoubleVar(value=current.llm_temperature)
        self.llm_key = tk.StringVar()
        self.comms_shortcut_labels = [
            tk.StringVar(value=label) for label, _message in current.comms_shortcuts]
        self.comms_shortcut_messages = [
            tk.StringVar(value=message) for _label, message in current.comms_shortcuts]
        self.wrist_hand = tk.StringVar(value=current.wrist_hand.title())
        self.wrist_offset_x = tk.DoubleVar(value=current.wrist_offset_x * 100.0)
        self.wrist_offset_y = tk.DoubleVar(value=current.wrist_offset_y * 100.0)
        self.wrist_offset_z = tk.DoubleVar(value=current.wrist_offset_z * 100.0)
        self.wrist_pitch = tk.DoubleVar(value=current.wrist_pitch)
        self.wrist_yaw = tk.DoubleVar(value=current.wrist_yaw)
        self.wrist_roll = tk.DoubleVar(value=current.wrist_roll)

        self.volume_label = tk.StringVar()
        self.haptic_label = tk.StringVar()
        self.broadcast_label = tk.StringVar()
        self.wrist_position_labels = [tk.StringVar() for _ in range(3)]
        self.wrist_rotation_labels = [tk.StringVar() for _ in range(3)]
        self.service_status = tk.StringVar()
        self.device_status = tk.StringVar()
        self.spotify_status = tk.StringVar()
        self.llm_status = tk.StringVar()
        self.save_status = tk.StringVar(value="Settings are shared with the wrist controls.")
        self._build()
        self.refresh_devices()
        self._update_labels()
        self._refresh_integration_status()
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
        style.configure("Section.Panel.TLabel", background=self.PANEL, foreground=self.CYAN,
                        font=("Segoe UI Semibold", 10))
        style.configure("Accent.TButton", background=self.VIOLET, foreground="white",
                        borderwidth=0, padding=(18, 10))
        style.map("Accent.TButton", background=[("active", "#8b6af0")])
        style.configure("TButton", background="#19243a", foreground=self.TEXT,
                        borderwidth=0, padding=(12, 8))
        style.configure("TEntry", fieldbackground="#0b1220", foreground=self.TEXT,
                        insertcolor=self.CYAN, padding=8)
        style.configure("TCombobox", fieldbackground="#0b1220", background="#19243a",
                        foreground=self.TEXT, arrowcolor=self.CYAN, padding=8)
        style.configure("Horizontal.TScale", background=self.PANEL, troughcolor="#202c43")
        style.configure("TCheckbutton", background=self.PANEL, foreground=self.TEXT)
        style.configure("TNotebook", background=self.BG, borderwidth=0)
        style.configure("TNotebook.Tab", background="#151e31", foreground=self.MUTED,
                        padding=(18, 9), borderwidth=0)
        style.map("TNotebook.Tab", background=[("selected", self.PANEL)],
                  foreground=[("selected", self.TEXT)])

    @staticmethod
    def _section(panel: ttk.Frame, row: int, title: str) -> int:
        if row:
            ttk.Separator(panel).grid(row=row, column=0, columnspan=2,
                                      sticky="ew", pady=(16, 14))
            row += 1
        ttk.Label(panel, text=title, style="Section.Panel.TLabel").grid(
            row=row, column=0, columnspan=2, sticky="w", pady=(0, 8))
        return row + 1

    def _build(self) -> None:
        outer = ttk.Frame(self.root, padding=24)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="INTERFAYCE", style="Title.TLabel").pack(anchor="w")
        ttk.Label(outer, text="Desktop configuration", foreground=self.CYAN).pack(
            anchor="w", pady=(0, 14))

        notebook = ttk.Notebook(outer)
        notebook.pack(fill="both", expand=True)
        general = ttk.Frame(notebook, style="Panel.TFrame", padding=20)
        integrations = ttk.Frame(notebook, style="Panel.TFrame", padding=20)
        comms = ttk.Frame(notebook, style="Panel.TFrame", padding=20)
        wrist = ttk.Frame(notebook, style="Panel.TFrame", padding=20)
        notebook.add(general, text="GENERAL")
        notebook.add(integrations, text="INTEGRATIONS")
        notebook.add(comms, text="COMMS")
        notebook.add(wrist, text="WRIST")
        self._build_general(general)
        self._build_integrations(integrations)
        self._build_comms(comms)
        self._build_wrist(wrist)

        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(14, 0))
        ttk.Label(footer, textvariable=self.service_status, foreground=self.MUTED).pack(side="left")
        ttk.Label(footer, textvariable=self.save_status, foreground=self.MUTED).pack(
            side="left", padx=20)
        ttk.Button(footer, text="Apply", style="Accent.TButton", command=self.apply).pack(side="right")

    def _build_general(self, panel: ttk.Frame) -> None:
        row = self._section(panel, 0, "AUDIO DEVICES")
        ttk.Label(panel, text="Voice input", style="Panel.TLabel").grid(row=row, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.device_status, style="Muted.Panel.TLabel").grid(
            row=row, column=1, sticky="e")
        row += 1
        self.microphone_box = ttk.Combobox(panel, textvariable=self.microphone, state="readonly")
        self.microphone_box.grid(row=row, column=0, columnspan=2, sticky="ew", pady=(5, 8))
        row += 1
        ttk.Label(panel, text="Spoken-response output", style="Panel.TLabel").grid(
            row=row, column=0, columnspan=2, sticky="w")
        row += 1
        self.output_box = ttk.Combobox(panel, textvariable=self.tts_output, state="readonly")
        self.output_box.grid(row=row, column=0, columnspan=2, sticky="ew", pady=(5, 8))
        row += 1
        ttk.Button(panel, text="Refresh devices", command=self.refresh_devices).grid(
            row=row, column=0, sticky="w")

        row = self._section(panel, row + 1, "SPOKEN RESPONSES")
        ttk.Label(panel, text="Volume", style="Panel.TLabel").grid(row=row, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.volume_label, style="Muted.Panel.TLabel").grid(
            row=row, column=1, sticky="e")
        row += 1
        ttk.Scale(panel, from_=0, to=100, variable=self.volume,
                  command=lambda _value: self._update_labels()).grid(
                      row=row, column=0, columnspan=2, sticky="ew", pady=(5, 4))
        row += 1
        ttk.Checkbutton(panel, text="Mute spoken responses", variable=self.muted).grid(
            row=row, column=0, sticky="w")
        ttk.Label(panel, text="Speed", style="Panel.TLabel").grid(row=row, column=1, sticky="w", padx=(60, 0))
        ttk.Spinbox(panel, from_=0.25, to=4.0, increment=0.05, textvariable=self.speed,
                    width=8).grid(row=row, column=1, sticky="e")

        row = self._section(panel, row + 1, "VR FEEDBACK")
        ttk.Label(panel, text="Haptic strength", style="Panel.TLabel").grid(row=row, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.haptic_label, style="Muted.Panel.TLabel").grid(
            row=row, column=1, sticky="e")
        row += 1
        ttk.Scale(panel, from_=0, to=100, variable=self.haptic,
                  command=lambda _value: self._update_labels()).grid(
                      row=row, column=0, columnspan=2, sticky="ew", pady=(5, 8))
        row += 1
        ttk.Label(panel, text="Broadcast boost", style="Panel.TLabel").grid(row=row, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.broadcast_label, style="Muted.Panel.TLabel").grid(
            row=row, column=1, sticky="e")
        row += 1
        ttk.Scale(panel, from_=0, to=24, variable=self.broadcast_gain,
                  command=lambda _value: self._update_labels()).grid(
                      row=row, column=0, columnspan=2, sticky="ew", pady=(5, 0))
        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=1)

    def _build_integrations(self, panel: ttk.Frame) -> None:
        row = self._section(panel, 0, "KOKORO TTS")
        row = self._entry_row(panel, row, "Speech endpoint", self.tts_endpoint)
        row = self._entry_row(panel, row, "Model", self.tts_model)
        row = self._entry_row(panel, row, "Voice or blend", self.tts_voice)

        row = self._section(panel, row, "SPOTIFY OAUTH")
        row = self._entry_row(panel, row, "Client ID", self.spotify_client_id)
        ttk.Label(panel, textvariable=self.spotify_status, style="Muted.Panel.TLabel").grid(
            row=row, column=0, sticky="w")
        buttons = ttk.Frame(panel, style="Panel.TFrame")
        buttons.grid(row=row, column=1, sticky="e")
        ttk.Button(buttons, text="Connect", command=self.connect_spotify).pack(side="left")
        ttk.Button(buttons, text="Disconnect", command=self.disconnect_spotify).pack(
            side="left", padx=(8, 0))
        row += 1

        row = self._section(panel, row, "CONVERSATIONAL LLM")
        ttk.Checkbutton(panel, text="Enable LLM fallback for unrecognized Music commands",
                        variable=self.llm_enabled).grid(row=row, column=0, columnspan=2, sticky="w")
        row += 1
        row = self._entry_row(panel, row, "OpenAI-compatible endpoint", self.llm_endpoint)
        row = self._entry_row(panel, row, "Model", self.llm_model)
        row = self._entry_row(panel, row, "Reasoning effort (optional)", self.llm_reasoning)
        ttk.Label(panel, text="Temperature", style="Panel.TLabel").grid(row=row, column=0, sticky="w", pady=4)
        ttk.Spinbox(panel, from_=0.0, to=2.0, increment=0.05,
                    textvariable=self.llm_temperature, width=10).grid(row=row, column=1, sticky="ew", pady=4)
        row += 1
        ttk.Label(panel, text="API key", style="Panel.TLabel").grid(row=row, column=0, sticky="w", pady=4)
        key_row = ttk.Frame(panel, style="Panel.TFrame")
        key_row.grid(row=row, column=1, sticky="ew", pady=4)
        ttk.Entry(key_row, textvariable=self.llm_key, show="●").pack(side="left", fill="x", expand=True)
        ttk.Button(key_row, text="Save", command=self.save_llm_key).pack(side="left", padx=(8, 0))
        ttk.Button(key_row, text="Remove", command=self.remove_llm_key).pack(side="left", padx=(8, 0))
        row += 1
        ttk.Label(panel, textvariable=self.llm_status, style="Muted.Panel.TLabel").grid(
            row=row, column=0, columnspan=2, sticky="w", pady=(4, 0))
        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=2)

    def _build_comms(self, panel: ttk.Frame) -> None:
        row = self._section(panel, 0, "OSC CHATBOX SHORTCUTS")
        ttk.Label(panel, text="Short labels appear on the wrist. Messages are sent immediately to VRChat.",
                  style="Muted.Panel.TLabel").grid(
                      row=row, column=0, columnspan=2, sticky="w", pady=(0, 14))
        row += 1
        ttk.Label(panel, text="WRIST LABEL (12 MAX)", style="Muted.Panel.TLabel").grid(
            row=row, column=0, sticky="w")
        ttk.Label(panel, text="CHATBOX MESSAGE (144 MAX)", style="Muted.Panel.TLabel").grid(
            row=row, column=1, sticky="w")
        row += 1
        for index in range(4):
            ttk.Entry(panel, textvariable=self.comms_shortcut_labels[index], width=16).grid(
                row=row, column=0, sticky="ew", padx=(0, 10), pady=6)
            ttk.Entry(panel, textvariable=self.comms_shortcut_messages[index]).grid(
                row=row, column=1, sticky="ew", pady=6)
            row += 1
        ttk.Label(panel, text="A slot with a blank label or message stays disabled on the wrist.",
                  style="Muted.Panel.TLabel").grid(
                      row=row, column=0, columnspan=2, sticky="w", pady=(14, 0))
        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=4)

    def _build_wrist(self, panel: ttk.Frame) -> None:
        row = self._section(panel, 0, "HANDEDNESS")
        ttk.Label(panel, text="Panel wrist", style="Panel.TLabel").grid(
            row=row, column=0, sticky="w")
        hand = ttk.Combobox(panel, textvariable=self.wrist_hand,
                            values=("Left", "Right"), state="readonly", width=12)
        hand.grid(row=row, column=1, sticky="e")
        row += 1
        ttk.Label(panel, text="The opposite hand automatically becomes the wrist UI pointer.",
                  style="Muted.Panel.TLabel").grid(
                      row=row, column=0, columnspan=2, sticky="w", pady=(6, 0))

        row = self._section(panel, row + 1, "POSITION OFFSET")
        position = (
            ("Sideways", self.wrist_offset_x, self.wrist_position_labels[0]),
            ("Up / down", self.wrist_offset_y, self.wrist_position_labels[1]),
            ("Stand-off", self.wrist_offset_z, self.wrist_position_labels[2]),
        )
        for title, variable, label in position:
            ttk.Label(panel, text=title, style="Panel.TLabel").grid(row=row, column=0, sticky="w")
            ttk.Label(panel, textvariable=label, style="Muted.Panel.TLabel").grid(
                row=row, column=1, sticky="e")
            row += 1
            ttk.Scale(panel, from_=-10, to=10, variable=variable,
                      command=lambda _value: self._update_labels()).grid(
                          row=row, column=0, columnspan=2, sticky="ew", pady=(3, 8))
            row += 1

        row = self._section(panel, row, "ROTATION OFFSET")
        rotation = (
            ("Pitch", self.wrist_pitch, self.wrist_rotation_labels[0]),
            ("Yaw", self.wrist_yaw, self.wrist_rotation_labels[1]),
            ("Roll", self.wrist_roll, self.wrist_rotation_labels[2]),
        )
        for title, variable, label in rotation:
            ttk.Label(panel, text=title, style="Panel.TLabel").grid(row=row, column=0, sticky="w")
            ttk.Label(panel, textvariable=label, style="Muted.Panel.TLabel").grid(
                row=row, column=1, sticky="e")
            row += 1
            ttk.Scale(panel, from_=-45, to=45, variable=variable,
                      command=lambda _value: self._update_labels()).grid(
                          row=row, column=0, columnspan=2, sticky="ew", pady=(3, 8))
            row += 1
        ttk.Button(panel, text="Reset offsets", command=self._reset_wrist_offsets).grid(
            row=row, column=0, sticky="w", pady=(4, 0))
        ttk.Label(panel, text="Apply updates the running wrist panel without a restart.",
                  style="Muted.Panel.TLabel").grid(row=row, column=1, sticky="e", pady=(4, 0))
        panel.columnconfigure(0, weight=1)
        panel.columnconfigure(1, weight=1)

    def _reset_wrist_offsets(self) -> None:
        for variable in (self.wrist_offset_x, self.wrist_offset_y, self.wrist_offset_z,
                         self.wrist_pitch, self.wrist_yaw, self.wrist_roll):
            variable.set(0.0)
        self._update_labels()

    @staticmethod
    def _entry_row(panel: ttk.Frame, row: int, label: str, variable: tk.StringVar) -> int:
        ttk.Label(panel, text=label, style="Panel.TLabel").grid(row=row, column=0, sticky="w", pady=4)
        ttk.Entry(panel, textvariable=variable).grid(row=row, column=1, sticky="ew", pady=4)
        return row + 1

    def _update_labels(self) -> None:
        self.volume_label.set(f"{round(self.volume.get())}%")
        self.haptic_label.set(f"{round(self.haptic.get())}%")
        self.broadcast_label.set(f"+{round(self.broadcast_gain.get())} dB")
        for label, variable in zip(self.wrist_position_labels,
                                   (self.wrist_offset_x, self.wrist_offset_y,
                                    self.wrist_offset_z)):
            label.set(f"{variable.get():+.1f} cm")
        for label, variable in zip(self.wrist_rotation_labels,
                                   (self.wrist_pitch, self.wrist_yaw, self.wrist_roll)):
            label.set(f"{variable.get():+.0f}°")

    @staticmethod
    def _select_device(box: ttk.Combobox, variable: tk.StringVar,
                       devices: list[str], default_label: str) -> None:
        current = variable.get()
        values = [default_label] + devices
        if current and current not in devices and current != default_label:
            values.append(current)
        box["values"] = values
        variable.set(current if current else default_label)

    def refresh_devices(self) -> None:
        inputs = microphone_names()
        outputs = output_device_names()
        self._select_device(self.microphone_box, self.microphone, inputs, "System default")
        self._select_device(self.output_box, self.tts_output, outputs, "System default")
        self.device_status.set(f"{len(inputs)} inputs / {len(outputs)} outputs")

    def _persist(self) -> bool:
        tts_endpoint = self.tts_endpoint.get().strip()
        llm_endpoint = self.llm_endpoint.get().strip()
        if tts_endpoint and not _valid_http_url(tts_endpoint):
            messagebox.showerror("Interfayce", "Kokoro needs a valid HTTP or HTTPS endpoint.", parent=self.root)
            return False
        if llm_endpoint and not _valid_http_url(llm_endpoint):
            messagebox.showerror("Interfayce", "The LLM needs a valid HTTP or HTTPS endpoint.", parent=self.root)
            return False
        selected_input = self.microphone.get()
        selected_output = self.tts_output.get()
        set_desktop_configuration(
            tts_volume=self.volume.get() / 100.0,
            tts_muted=self.muted.get(),
            tts_speed=self.speed.get(),
            tts_endpoint=tts_endpoint,
            tts_model=self.tts_model.get(),
            tts_voice=self.tts_voice.get(),
            tts_output="" if selected_output == "System default" else selected_output,
            stt_microphone="" if selected_input == "System default" else selected_input,
            haptic_strength=self.haptic.get() / 100.0,
            broadcast_gain_db=round(self.broadcast_gain.get()),
            spotify_client_id=self.spotify_client_id.get(),
            llm_enabled=self.llm_enabled.get(),
            llm_endpoint=llm_endpoint,
            llm_model=self.llm_model.get(),
            llm_reasoning_effort=self.llm_reasoning.get(),
            llm_temperature=self.llm_temperature.get(),
            comms_shortcuts=tuple(zip(
                (item.get() for item in self.comms_shortcut_labels),
                (item.get() for item in self.comms_shortcut_messages),
            )),
            wrist_hand=self.wrist_hand.get().casefold(),
            wrist_offset_x=self.wrist_offset_x.get() / 100.0,
            wrist_offset_y=self.wrist_offset_y.get() / 100.0,
            wrist_offset_z=self.wrist_offset_z.get() / 100.0,
            wrist_pitch=self.wrist_pitch.get(),
            wrist_yaw=self.wrist_yaw.get(),
            wrist_roll=self.wrist_roll.get(),
        )
        return True

    def apply(self) -> None:
        if not self._persist():
            return
        self.save_status.set("Applied")
        self._refresh_integration_status()
        self.root.after(1800, lambda: self.save_status.set(
            "Settings are shared with the wrist controls."))

    def save_llm_key(self) -> None:
        try:
            set_api_key(self.llm_key.get())
        except ValueError as error:
            messagebox.showerror("Interfayce", str(error), parent=self.root)
            return
        self.llm_key.set("")
        self._refresh_integration_status()
        self.save_status.set("LLM key protected and saved")

    def remove_llm_key(self) -> None:
        delete_api_key()
        self.llm_key.set("")
        self._refresh_integration_status()
        self.save_status.set("LLM key removed")

    def connect_spotify(self) -> None:
        if not self._persist():
            return
        client_id = self.spotify_client_id.get().strip()
        if not client_id:
            messagebox.showerror("Interfayce", "Enter the Spotify client ID first.", parent=self.root)
            return
        self.spotify_status.set("Waiting for browser authorization…")

        def worker() -> None:
            try:
                connect_spotify(client_id)
                profile = SpotifyWebApi(client_id).profile()
                result = f"Connected as {profile.display_name}"
            except Exception as error:
                result = f"Connection failed: {error}"
            self.root.after(0, lambda: self.spotify_status.set(result))

        threading.Thread(target=worker, name="InterfayceSpotifyConnect", daemon=True).start()

    def disconnect_spotify(self) -> None:
        disconnected = disconnect_spotify()
        self.spotify_status.set("Disconnected" if disconnected else "No saved Spotify session")

    def _refresh_integration_status(self) -> None:
        try:
            spotify_stored = load_token() is not None
        except SpotifyOAuthError:
            spotify_stored = False
        self.spotify_status.set("Protected session stored" if spotify_stored else "Not connected")
        key_stored = load_api_key() is not None
        if not self.llm_enabled.get():
            state = "Disabled"
        elif key_stored and self.llm_endpoint.get().strip() and self.llm_model.get().strip():
            state = "Enabled and configured"
        else:
            state = "Enabled, but configuration is incomplete"
        self.llm_status.set(f"{state} • API key {'stored securely' if key_stored else 'not stored'}")

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
