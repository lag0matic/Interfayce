# Interfayce

<p align="center">
  <img src="assets/branding/interfayce-icon-1024.png" width="160" alt="Interfayce logo">
</p>

Interfayce is a lightweight Windows wrist interface for SteamVR and VRChat. It brings desktop windows, media controls, speech tools, SlimeVR status, playspace controls, and an optional conversational assistant into one compact overlay designed to remain readable and responsive in a headset.

The project is personal-use-first. It is developed and tested around a Valve Index with Knuckles controllers, VRChat, SlimeVR, and the maintainer's own Windows setup. It is provided as-is and is not intended to be a universal VR dashboard or a supported commercial product.

## Development disclosure

Interfayce is developed with substantial AI assistance. Lag0Matic supplies the original concept, requirements, visual direction, product decisions, and live VR testing. OpenAI Codex/ChatGPT has contributed substantial implementation, refactoring, debugging, research, and documentation. This repository does not represent the project as solely hand-written by the maintainer.

## Features

### Wrist interface

- Compact Holo Glass interface that fades when the wrist is turned away.
- Music, Comms, ASK, Desk, Space, Rig, and Settings panels.
- Configurable left- or right-wrist placement.
- Local clock, lowest-tracker battery status, and concise service indicators.
- System-tray controls for Settings, restart, and clean shutdown.

### Interactive desktop surfaces

- Capture a physical display or a filtered list of visible applications.
- Move a surface with one grip and resize it by stretching with two grips.
- Left-click, drag, and scroll captured applications from VR.
- Spawn an independent, ambidextrous QWERTY keyboard with key glow, haptic feedback, copy, and paste.
- Lock, close, return to source selection, or bring a lost surface back into view.
- Configure three application favorites; unused slots are filled by a bounded, deduplicated recent-app history.
- Desk surfaces hide when leaving the Desk panel and return when it is selected again.
- Closing an overlay surface does **not** close its underlying application.

### Music and Spotify

- Play/pause, previous, next, current-track status, and local media-session control.
- Optional Spotify OAuth with PKCE for search, volume, and natural-language requests.
- Optional OpenAI-compatible LLM routing for flexible commands such as song requests and relative volume changes.
- Optional spoken responses through an OpenAI-compatible Kokoro TTS server.
- Current-track announcements through the VRChat OSC chatbox.
- Optional Spotify-only broadcast to VRChat through VB-CABLE, with an independent gain control.

Basic transport and track status use the Windows media session and do not require Spotify OAuth. Search, direct playback requests, and Spotify volume control do.

### Speech and communication

- Push-to-listen speech transcription into the VRChat OSC chatbox.
- Configurable automatic stop after a period of silence.
- Clear-chat control and four configurable canned OSC shortcuts.
- Bundled local Parakeet speech recognition; no COVAS installation is required by the packaged app.
- Optional remote Faster-Whisper service for improved recognition of artist and song names, with local Parakeet fallback.

### ASK assistant

- Conversational, spoken questions and TTS answers from the wrist.
- Uses the configured OpenAI-compatible LLM endpoint.
- Deterministic tools for calculations, time, and weather.
- Optional current-information research through the Brave Search API.
- Concise citations and visible status feedback when web research is used.

### SlimeVR and playspace tools

- Tracker battery levels, mount readiness, reset/mount controls, and a compact body scanner view.
- Estimated remaining tracker runtime after enough battery history has been observed.
- Deduplicated spoken low- and critical-battery warnings.
- Session-only playspace movement with a configurable travel limit.
- Restores the startup playspace baseline rather than rewriting SteamVR's room calibration.

## Requirements

### Core

- Windows 10 or 11, x64.
- SteamVR/OpenVR.
- A Valve Index and Knuckles controllers for the supplied bindings.
- VRChat with OSC enabled for chatbox transcription, shortcuts, and track announcements.

### Optional integrations

| Integration | Used for |
| --- | --- |
| SlimeVR Server | Tracker batteries, mount readiness, and rig controls |
| Spotify desktop app | Music status and local transport |
| Spotify developer application | Search, direct playback, and volume through OAuth |
| VB-CABLE | Broadcasting Spotify audio into VRChat |
| Kokoro or compatible TTS server | Spoken assistant responses and warnings |
| OpenAI-compatible LLM provider | Natural-language Spotify control and ASK |
| Brave Search API | Current web research in ASK |
| Interfayce Remote STT | Optional Faster-Whisper transcription on another PC |

Other controllers, operating systems, tracker systems, and OpenXR-only runtimes are not currently supported targets.

## Installing Interfayce

Interfayce does not currently publish a general public release. If you received a trusted `Interfayce-Setup-*.exe` from the maintainer:

1. Start SteamVR.
2. Run the installer. It installs per-user to `%LOCALAPPDATA%\Programs\Interfayce` and does not require elevation.
3. Launch Interfayce from the Start menu.
4. Turn the inside of the configured wrist toward the headset to reveal the panel.
5. Open the gear panel and select the desktop-settings icon to configure integrations and devices.

Upgrades preserve settings. Uninstalling also leaves personal state under `%LOCALAPPDATA%\Interfayce` so a normal reinstall does not erase the configuration.

### First-run checklist

1. Enable OSC in VRChat if Comms or music announcements will be used.
2. Open desktop Settings and select the microphone, TTS output, wrist hand, and preferred haptic strength.
3. Start SlimeVR Server before Interfayce if rig information is wanted.
4. Start Spotify before testing Music.
5. Open Diagnostics in Settings and resolve any required service marked offline.
6. Spawn one Desk surface and the keyboard, then verify clicking, typing, moving, and resizing.

## Configuration

The desktop Settings window opens only when requested from the wrist or system tray. It contains:

- audio input and TTS output devices;
- local or remote STT settings and the Comms silence timeout;
- Kokoro endpoint, model, voice, speed, and volume;
- Spotify OAuth connection;
- LLM endpoint, model, and feature toggle;
- Brave Search credentials;
- VB-CABLE broadcast gain;
- desktop favorites and Comms shortcuts;
- wrist placement, playspace limit, and haptic strength;
- diagnostics and version information.

Non-secret settings are stored in:

```text
%LOCALAPPDATA%\Interfayce\settings.json
```

Credentials and OAuth tokens are stored separately using Windows DPAPI under the current Windows account. API keys, tokens, personal endpoints, and LAN addresses are not built into the application or installer.

### Spotify OAuth

Create or reuse a Spotify developer application with Web API access and register this exact redirect URI:

```text
http://127.0.0.1:8888/callback
```

Enter the client ID in Settings and complete the browser login. Interfayce uses Authorization Code with PKCE, so no Spotify client secret is required or stored. While the Spotify application is in development mode, the account using Interfayce must be included in its allowed users.

### LLM and Brave Search

LLM features are disabled by default. Configure an OpenAI-compatible endpoint, model, and protected API key before enabling them. The same LLM profile serves Music command interpretation and ASK.

Brave Search is optional. Without it, ASK can still use its built-in calculation, time, and weather tools, but it cannot perform general current-information research.

### Kokoro TTS

Interfayce expects an OpenAI-compatible speech endpoint:

```text
POST /v1/audio/speech
```

Configure the full endpoint, model, voice, playback device, speed, and volume in Settings. The TTS server may run locally or on a trusted private LAN.

### Remote STT

The packaged app includes local Parakeet STT. For better recognition of difficult names, the optional portable Windows server in [`remote-stt`](remote-stt/README.md) exposes an OpenAI-compatible transcription endpoint and can run Faster-Whisper on another machine.

Keep the remote server on a trusted LAN; do not expose its port directly to the internet. Enter its URL, model, and generated API key in Settings.

### Spotify broadcast through VB-CABLE

1. Install the production VB-CABLE driver and reboot if requested.
2. Select `CABLE Output (VB-Audio Virtual Cable)` as VRChat's microphone.
3. Use the broadcast control on the Music panel.
4. Adjust broadcast boost in Settings if needed.

`CABLE Input` is the playback endpoint used by Interfayce. Broadcast gain changes only the cable feed and does not raise normal Spotify listening volume. No WDK, custom audio driver, test certificate, or Windows test mode is required.

## Controls

The supplied SteamVR bindings are designed for Knuckles controllers.

| Action | Control |
| --- | --- |
| Point at an Interfayce control | Controller aim pose |
| Activate a control or desktop click | Trigger |
| Move a surface | Grip while pointing at that surface |
| Resize a surface | Grip the same surface with both hands and stretch or compact |
| Scroll a desktop surface | Right thumbstick |
| Move the playspace | Double-tap and hold `B`, then drag |

Destructive and reset actions use a visible hold countdown to prevent accidental activation.

## Building from source

### Prerequisites

- Git
- Python 3.12
- CMake
- Visual Studio 2022 C++ Build Tools with a current Windows SDK
- Inno Setup 6 for installer packaging
- Node.js/npm access during packaging; the release script stages its pinned runtime and SolarXR dependency

The Windows Driver Kit is **not** required.

Create the Python environment:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -e ".[voice]" pytest pyinstaller
```

The four Parakeet model files are intentionally excluded from Git because they total approximately 639 MB. Restore the files listed in [`models/parakeet/MODEL-SOURCE.md`](models/parakeet/MODEL-SOURCE.md) before packaging; the document includes their expected SHA-256 hashes and upstream provenance.

Run the test suite:

```powershell
$env:PYTHONPATH = "$PWD\src"
python -m pytest -q
```

Build the native applications:

```powershell
cmake -S native -B native\build
cmake --build native\build --config Release
```

Build the installer:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\build-installer.ps1
```

The installer is written beneath `packaging\out\installer`.

## Troubleshooting

### SlimeVR is shown as offline

Confirm SlimeVR Server is running and listening on its normal local service port. The installed build contains the pinned SolarXR adapter. A raw `native\build\bin\InterfayceOverlay.exe` development launch does not include that staged dependency and can report SlimeVR offline even while the server is healthy.

### Speech is missing or slow

- Verify the configured microphone and TTS playback device.
- Test remote STT or Kokoro directly from the VR PC.
- Confirm Windows Firewall permits trusted-LAN traffic to the server.
- Expect the first request after a model restart to be slower while it warms.
- Remove the remote STT endpoint temporarily to verify the bundled Parakeet fallback.

### Broadcast is silent

Confirm VRChat is listening to `CABLE Output`, Windows has not muted the cable input, and Spotify itself is playing. The broadcast control captures Spotify specifically rather than all desktop audio.

### SteamVR input changed unexpectedly

Restart Interfayce after installing a build with updated SteamVR actions or bindings. SteamVR may retain the prior action manifest for the existing process.

Logs are stored under:

```text
%LOCALAPPDATA%\Interfayce\logs
```

For clean-machine recovery, protected-setting behavior, server setup, and detailed validation steps, see [`RECOVERY.md`](RECOVERY.md).

## Security and privacy

- Spotify uses OAuth PKCE; no client secret is needed.
- API keys and OAuth tokens use Windows DPAPI and are not stored in `settings.json`.
- LLM and research features are opt-in and disabled until configured.
- Remote STT and TTS are intended for a trusted private LAN.
- Voice audio is sent to the configured STT endpoint only while a listening feature is active.
- No telemetry service is built into Interfayce.

Review the endpoint and provider privacy policies before enabling cloud services.

## Project status and scope

Interfayce is actively used and playtested, but remains a personal project. Compatibility is centered on the maintainer's setup, and no general support commitment is made. Bug reports and careful technical discussion are welcome; feature work is prioritized around real VR use.

No general-use license has been granted at this time. Do not assume permission to redistribute the source or packaged installer.

Additional project references:

- [`RECOVERY.md`](RECOVERY.md) — recovery, reinstall, and integration checklist
- [`TECHNICAL-NOTES.md`](TECHNICAL-NOTES.md) — implementation notes and architecture details
- [`PROJECT-NORTH-STAR.md`](PROJECT-NORTH-STAR.md) — product direction and design principles
- [`remote-stt/README.md`](remote-stt/README.md) — portable Windows Faster-Whisper service
