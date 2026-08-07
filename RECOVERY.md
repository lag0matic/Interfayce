# Interfayce recovery and rebuild guide

This is the practical checklist for recovering Interfayce after a clean Windows
installation, replacing the VR PC, or rebuilding the project after a long gap.
The normal recovery path is to install the latest known-good installer. Building
from source is only necessary when producing a new release.

## What to preserve before resetting Windows

Back up these non-secret files if the old installation is still readable:

- `%LOCALAPPDATA%\Interfayce\settings.json` — endpoints, models, device choices,
  wrist placement, favorites, shortcuts, volume, haptics, and feature toggles.
- `%LOCALAPPDATA%\Interfayce\desktop-history.json` — the bounded recent-window
  list. This is convenient but disposable.
- The latest `Interfayce-Setup-*.exe` and its SHA-256 from `BUILD-STATUS.md`.
- The complete `remote-stt` directory from the speech server if that machine is
  also being rebuilt. Its ignored `config.json` contains the generated API key.

Secrets are stored as DPAPI blobs under `%LOCALAPPDATA%\Interfayce\secure`.
They are deliberately tied to the Windows user and machine protection context.
They may remain readable after an ordinary in-place app upgrade, but do not rely
on them after a clean Windows installation, account replacement, or migration to
another PC. Plan to re-enter or reauthorize:

- Spotify OAuth;
- the LLM provider API key;
- the Brave Search API key;
- the remote-STT API key.

Do not commit `settings.json`, `config.json`, `.dpapi` files, API keys, OAuth
tokens, private LAN addresses, or device-specific paths to GitHub.

## Clean-machine restore order

1. Install current GPU, chipset, audio, and Index/SteamVR device drivers.
2. Install Steam and SteamVR, then VRChat. Enable OSC in VRChat when using Comms
   transcription, shortcuts, or Spotify track announcements.
3. Install and configure SlimeVR Server. Its SolarXR service normally listens on
   local port `21110`; tracker batteries and mount readiness appear only while
   the server and its trackers are available.
4. Install Spotify and sign in. Local play/pause/previous/next controls work from
   the Windows media session; search, natural-language requests, and volume use
   Spotify OAuth/Web API.
5. Install the production-signed VB-CABLE package only if Spotify broadcast into
   VRChat is wanted, then reboot if its installer requests it.
6. Run the latest Interfayce installer. It is per-user and installs to
   `%LOCALAPPDATA%\Programs\Interfayce` without elevation.
7. Open desktop Settings from the tray or wrist Settings deck and restore the
   non-secret configuration. Re-enter secrets through the protected dialogs.
8. Restore or reinstall the optional remote-STT and Kokoro servers, then verify
   them independently before troubleshooting Interfayce.
9. Run the checks below before the first long VR session.

Interfayce upgrades deliberately preserve `%LOCALAPPDATA%\Interfayce`. An
uninstall also leaves that personal state behind so an ordinary reinstall does
not erase configuration.

## Integration setup and common traps

### Spotify OAuth

The Spotify developer application needs Web API access and this exact redirect:

```text
http://127.0.0.1:8888/callback
```

Interfayce uses Authorization Code with PKCE; no client secret is required or
stored. Enter the client ID in Settings, complete Connect Spotify in a desktop
browser, and ensure the Spotify account is allowed as a user while the developer
application remains in development mode.

### LLM and ASK research

The LLM is disabled by default. Configure an OpenAI-compatible endpoint, model,
and protected API key, then explicitly enable it. ASK uses the same LLM profile.
Current web research additionally requires a Brave Search API key entered through
the protected Assistant Search dialog. Without Brave, ASK still has deterministic
calculation, time, and weather tools but cannot perform general web research.

Never put provider keys or personal endpoints in source, installer scripts, or
example configuration.

### Speech recognition and Kokoro

The installer contains the local Parakeet model and needs no COVAS installation.
Remote STT is optional; when configured but unavailable, Interfayce falls back to
bundled Parakeet. The portable server instructions are in
[`remote-stt/README.md`](remote-stt/README.md).

On the remote server:

- port `5010` must be reachable from the VR PC on the trusted LAN;
- `config.json` supplies the API key and is intentionally ignored by Git;
- the first model start can be slow while files download and CUDA warms;
- an immediate `status.ps1` after `start.ps1` can race startup—retry once;
- Faster-Whisper needs CUDA 12 cuBLAS and cuDNN 9. Use the supplied
  `import-cuda-runtime.ps1` helper rather than downloading loose DLLs;
- rerun `autostart.ps1 install` after moving the portable directory.

Kokoro is optional and uses an OpenAI-style `POST /v1/audio/speech` endpoint.
Configure its URL, model, voice, output device, speed, and volume in Settings.
Test the server directly if speech is absent; a cold model may make the first
request slower than later requests.

### VB-CABLE broadcast

Select `CABLE Output (VB-Audio Virtual Cable)` as VRChat's microphone when using
the Music broadcast control. `CABLE Input` is the playback side targeted by the
Interfayce audio engine. Windows input volume for the cable must not be muted or
near zero. Broadcast gain affects only the cable path and does not change normal
Spotify listening volume.

No WDK, custom Interfayce driver, development certificate, or Windows test mode
is required. Production uses VB-CABLE. If an old experiment left test signing
enabled, disable it with an elevated `bcdedit /set testsigning off` and reboot.

### SlimeVR reports offline while the server is open

The installed application bundles the pinned SolarXR adapter and its private
Node runtime. A raw `native\build\bin\InterfayceOverlay.exe` development launch
does not contain that staged dependency and can therefore report SlimeVR offline
even while port `21110` is healthy. Use the installed build, run the installer
packaging script, or point `SLIMEVR_SERVER_SOURCE` at a directory containing
`solarxr-protocol`. Do not diagnose the Slime server from the raw-build status
alone.

## Post-restore checks

From PowerShell:

```powershell
$app = "$env:LOCALAPPDATA\Programs\Interfayce"
& "$app\InterfayceOverlay.exe" --version
& "$app\InterfayceOverlay.exe" --service-status
& "$app\InterfayceOverlay.exe" --slime-summary
& "$app\InterfayceOverlay.exe" --desktop-sources
```

Expected results:

- `--version` matches the installer and Add/Remove Programs;
- SlimeVR is available when its server is running;
- Spotify is running when its desktop client is open;
- `--slime-summary` returns tracker percentages rather than eight `--` slots;
- `--desktop-sources` lists displays and user-facing applications.

Open Settings and inspect Diagnostics. Then perform a short headset smoke test:

1. Music transport and one natural-language Spotify request.
2. One ASK calculation and one current-information query with a cited answer.
3. One Comms transcription and clear action with VRChat OSC enabled.
4. Spawn a desktop surface and keyboard; click, type, move, resize, hide, and
   restore them by leaving and returning to Desk.
5. Check Rig batteries and mount readiness.
6. Start and stop Spotify broadcast, confirming VRChat receives cable audio and
   the cable returns to silence afterward.

Logs are under `%LOCALAPPDATA%\Interfayce\logs`. Settings are intentionally opened
on demand rather than at every startup. Clean exit is available from the tray or
the wrist Settings shutdown control, which requires a three-second hold.

## Rebuilding an installer from source

Install Git, Python 3.12, CMake, Visual Studio C++ build tools with a current
Windows SDK, Inno Setup 6, and the Python project dependencies. The WDK is not
required. Then restore the four git-ignored Parakeet files described—with hashes
and upstream provenance—in [`models/parakeet/MODEL-SOURCE.md`](models/parakeet/MODEL-SOURCE.md).
This is the most likely clean-checkout packaging failure because the model is
approximately 639 MB and intentionally absent from Git history.

Run:

```powershell
$env:PYTHONPATH = "$PWD\src"
python -m pytest -q
powershell -ExecutionPolicy Bypass -File packaging\build-installer.ps1
```

The script builds Release native binaries, freezes the resident service, fetches
and pins SolarXR, stages Node and Parakeet, and creates the per-user installer.
Verify the generated SHA-256 and update `BUILD-STATUS.md` before publishing.

