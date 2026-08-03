# Interfayce

A personal, resource-conscious VRChat cockpit for David's SteamVR setup.

The project begins with the things that matter in an actual session:

- guarded, direct playspace drag;
- a tiny wrist cockpit for tracker, music, and performance state;
- one-shot Spotify announcements in VRChat's OSC chatbox;
- desktop apps that wake only when needed.

It is intentionally personal-use-first. We are building the useful little shipboard console, not a generic VR dashboard empire.

## Current build

The native C++20 host now provides the fading inner-wrist utility panel, safe session-only playspace movement, Spotify transport/status and conversational control, continuous local-STT Comms dictation to VRChat OSC, SlimeVR rig status and recovery, and independently movable/reusable interactive desktop surfaces with native application icons and an ambidextrous VR keyboard.

The Python support code can build and send VRChat chatbox OSC packets with no third-party dependency. `interfayce.song_announcer` formats and deduplicates the intended Spotify message:

```text
♫ Artist — Title
```

It reads Spotify through Windows' local media-session API; no Spotify OAuth or cloud service is required for the basic case.

Advanced Spotify control uses a browser-based PKCE login. The developer app must register the exact redirect URI `http://127.0.0.1:8888/callback`. Connect and verify with:

```powershell
$env:PYTHONPATH = "$PWD/src"
python -m interfayce spotify-oauth-connect --client-id YOUR_CLIENT_ID
python -m interfayce spotify-oauth-status
```

The client secret is not used. OAuth tokens are protected with Windows DPAPI under the current Windows account.

The desktop settings window opens from the monitor icon on the wrist Settings deck. It remains closed otherwise and owns audio devices, TTS behavior, haptics, broadcast gain, Kokoro, Spotify OAuth, and the optional constrained LLM fallback. Tokens and API keys are protected with Windows DPAPI; a fresh installation contains no personal endpoints and leaves the LLM disabled. For an explicit development launch, `python -m interfayce settings` opens the same single-instance window.

## Windows installer

The reproducible per-user installer bundles the native overlay/audio engine, the Python voice runtime, the pinned SolarXR adapter, and its isolated Node runtime:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\build-installer.ps1
```

The result is `packaging\out\installer\Interfayce-Setup-0.1.0.exe`. It installs under `%LOCALAPPDATA%\Programs\Interfayce` without elevation. Personal settings remain under `%LOCALAPPDATA%\Interfayce`; uninstalling or upgrading the application deliberately leaves them intact. VB-CABLE is optional for the rest of Interfayce but required for Spotify-to-VRChat broadcast.

## Offline native checks

These modes do not initialize SteamVR:

```powershell
native\build\bin\InterfayceOverlay.exe --service-status
native\build\bin\InterfayceOverlay.exe --desktop-sources
native\build\bin\InterfayceOverlay.exe --desktop-capture-probe
native\build\bin\InterfayceOverlay.exe --broadcast-controller-probe
native\build\bin\InterfayceAudioEngine.exe --probe-spotify 5
```

`--service-status` reports whether the local SlimeVR port and Spotify process are available. The capture probe requires an active Windows display but not a running headset session.
The audio-engine probe captures and meters only Spotify's process tree at 48 kHz stereo. It does not change Spotify's playback device, the Windows default device, or any SteamVR setting. The broadcast-controller probe verifies guarded start/stop against VB-CABLE without initializing SteamVR.

## Run the tests

Requires Python 3.11 or newer.

```powershell
$env:PYTHONPATH = "$PWD/src"
python -m unittest discover -s tests -v
```

## Send a one-time VRChat OSC smoke test

With VRChat running and OSC enabled:

```powershell
$env:PYTHONPATH = "$PWD/src"
python -m interfayce chatbox-test
```

Expected result: one silent message over your head: `♫ Test — Interfayce`.

## Announce Spotify track changes

With Spotify playing, VRChat running, and OSC enabled:

```powershell
$env:PYTHONPATH = "$PWD/src"
python -m interfayce spotify-watch
```

The song already playing when the watcher starts is intentionally ignored. A later track change must remain stable for three seconds before it sends one silent `♫ Artist — Title` message. Seven seconds later, Interfayce sends an experimental empty chatbox payload to clear it. Press `Ctrl+C` to stop it.

## Check Index controller batteries

With SteamVR running:

```powershell
$env:PYTHONPATH = "$PWD/src"
python -m interfayce steamvr-status
```

To read the current room-transform baseline without modifying it:

```powershell
python -m interfayce steamvr-baseline
```

## Documents

- [Project north star](PROJECT-NORTH-STAR.md)
- [Technical notes](TECHNICAL-NOTES.md)
- [Build status and handoff](BUILD-STATUS.md)

## Later improvements

1. Add active/glanceable/sleeping desktop-capture update policies.
2. Add deeper integration diagnostics only where live failures justify them.
3. Continue optional visual polish without expanding the always-awake footprint.
