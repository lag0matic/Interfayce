# Interfayce

A personal, resource-conscious VRChat cockpit for David's SteamVR setup.

The project begins with the things that matter in an actual session:

- guarded, direct playspace drag;
- a tiny wrist cockpit for tracker, music, and performance state;
- one-shot Spotify announcements in VRChat's OSC chatbox;
- desktop apps that wake only when needed.

It is intentionally personal-use-first. We are building the useful little shipboard console, not a generic VR dashboard empire.

## Current first slice

`interfayce.osc` can build and send VRChat chatbox OSC packets with no third-party dependency. `interfayce.song_announcer` formats and deduplicates the intended Spotify message:

```text
♫ Artist — Title
```

It reads Spotify through Windows' local media-session API; no Spotify OAuth or cloud service is required for the basic case.

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

## Near-term work

1. Build the minimal native C++20 SteamVR overlay host and verify its Index action input.
2. Add the D3D11 inner-wrist overlay texture after input is proven.
3. Implement guarded, temporary playspace drag with preview, undo, and restore-session-baseline.
4. Research the cleanest supported SlimeVR full-calibration integration.
