# Interfayce — Build Status

Updated: 2026-08-03

## Current checkpoint

- Branch: `codex/desktop-surfaces`
- Draft PR: https://github.com/lag0matic/Interfayce/pull/1
- Last published feature commit: `09d2ba3 feat: polish VR surfaces and service availability`
- Native host: `native/build/bin/InterfayceOverlay.exe`
- Python suite: 51 passing tests

The desktop interaction slice is now **usably complete**. Spotify OAuth, constrained conversational Music control, local STT, and Kokoro acknowledgments have also passed live in-headset testing.

## Desktop surfaces

Live-tested behavior:

- New Surface opens a world-space picker containing displays and filtered, user-facing applications.
- Selecting a source converts that picker into a live Windows Graphics Capture surface.
- Trigger input forwards pointer movement and primary clicks to captured content.
- The aiming guide is a small surface dot only. The old laser overlays and beam math have been removed because the beams gave misleading depth cues between nearby surfaces.
- The left controller now receives its own violet aim dot across desktop content and the narrow grab edge, without gaining desktop mouse-click authority.
- Either controller can grab a surface using deliberate Index force-grip input. One hand moves it; both hands stretch or compact it around their midpoint.
- The wrist list can bring a lost surface back or close its Interfayce overlay without closing the underlying application.
- Right-thumbstick scrolling forwards vertical and horizontal wheel input when VRChat does not consume that axis.
- Surface position and size are deliberately session-only; arrangements vary too much day to day to restore usefully.

The independent keyboard:

- is a wide, staggered QWERTY layout rather than SteamVR's modal keyboard;
- accepts trigger input from either hand with cyan/violet pointer dots;
- targets the most recently clicked captured surface;
- supports Shift, Ctrl, Alt, Backspace, Enter, arrows, and Space;
- gives a short key glow and a haptic tap on the hand that pressed it;
- uses direct panel geometry with only a small edge tolerance, allowing rays to pass naturally through the space between keyboard and monitor;
- participates in the same Bring to me, Close, one-hand move, and two-hand resize behavior as desktop surfaces.

## Wrist and visual state

- The chosen direction is **Orbital Utility**: smoked transparent panels, violet structure, cyan reserved for active state, icons before text, and restrained glow.
- Music uses circular icon controls and a compact playback activity meter.
- Music is the startup deck. Its media-session query refreshes asynchronously so helper startup cannot block wrist interaction; the last known state remains visible while it refreshes.
- Music status, artwork, and transport controls now reuse the resident localhost service. The old two-second `cmd.exe`/Python launch loop was removed because it caused Windows application-start cursor feedback.
- Playspace shows only baseline/adjusted session state and an abstract origin-reset glyph.
- A compact gear opens wrist settings. The first live controls are persistent Kokoro output volume and mute; they update the voice service immediately.
- Spotify OAuth uses Authorization Code with PKCE through the existing Covasify developer app and `http://127.0.0.1:8888/callback`. Live authorization, protected-token reload, account lookup, and Web API search all pass.
- Music keeps explicit transport commands on the local fast path and forwards only unrecognized requests to a constrained DeepInfra intent router. The provider key and Spotify tokens are protected with Windows DPAPI.
- Conversational play requests canonicalize artist names through Spotify, then require confident title and artist matches before playback. Ambiguous or unrelated search results fail closed.
- Spoken Music controls now cover search/play, transport, status, Spotify volume, mute, and unmute. Both successes and safe command failures receive short asynchronous Kokoro acknowledgments.
- The Music response line has its own full-width strip at the bottom of the deck, clear of the transport controls.
- Selected desktop and keyboard surfaces use subtle violet backlight rather than thick grab bars. Their functional grab regions are visually quiet.
- The wrist remains mounted to the left inner wrist using the live-fitted transform in `InnerLeftWristTransform()`.

## Startup capability gates

- At startup, Interfayce checks SlimeVR on `127.0.0.1:21110`. If unavailable, Slime tracker/reset controls are omitted for that play session instead of allowing the Rig tab to block.
- Spotify presence is checked natively through the running process list. Opening Music does not launch a slow helper when Spotify is absent.
- `--service-status` reports both checks without initializing SteamVR.
- `--desktop-sources` inventories capturable sources without SteamVR.
- `--desktop-capture-probe` verifies receipt of a real GPU frame without SteamVR.
- `--shutdown` asks a running host to exit through its normal session-baseline cleanup path.

## Constraints to preserve

- `OverlayRenderer` retains one D3D11/DXGI shared texture and repaints it in place. Do not recreate GPU devices or overlay textures for ordinary panel updates.
- Keep `InnerLeftWristTransform()` unchanged unless David explicitly starts another live fitting pass.
- Right trigger is UI click. Index B double-tap/hold is playspace drag only.
- Playspace movement may write only SteamVR's temporary working standing origin. Reset and normal shutdown restore the immutable startup snapshot; never call `CommitWorkingCopy`, infer a center from the HMD, or add floor/recenter controls without an explicit new decision.
- Closing a VR desktop surface must never close its source application.
- Do not reintroduce a magnetic target halo, timed surface handoff, or visual laser without a new live interaction reason.
- SlimeVR Node adapters are development-only and currently rely on a temporary reference checkout; vendor and version-pin the SolarXR protocol before packaging.

## Larger features intentionally next, not half-started

1. A separate Comms mic-button path for previewable VRChat chatbox dictation.
2. First-party process-specific music capture and virtual-microphone routing, isolated from the overlay host because it requires driver work and explicit safety decisions.
3. Desktop settings window for OAuth, integrations, device selection, and diagnostics. Non-secret preferences live under `%LOCALAPPDATA%\Interfayce`; tokens and API keys must use Windows credential protection rather than the JSON settings file.
4. Active/glanceable/sleeping capture update policies, wrist visibility/fade behavior, and the final feature-complete UI polish pass.

## Verification record

- Native Release build succeeds with SteamVR stopped.
- `--service-status` is the intended offline capability smoke test.
- Windows Graphics Capture previously returned a real display frame through `--desktop-capture-probe`.
- Picker selection, display/app capture, pointer clicks, vertical scrolling, ambidextrous typing, key feedback, one-hand movement, two-hand resizing, and wrist recovery controls have passed live VR testing.
- Spotify OAuth, conversational Music requests, generic artist-name correction, fail-closed track selection, Spotify volume control, and spoken success/failure responses have passed live testing.
- Python proof-of-concept suite: 51 passing checks.

## North star

Interfayce is David/Lag0Matic's personal, low-overhead VRChat cockpit, not a general dashboard product. Windows and SteamVR/OpenVR come first; Linux/OpenXR can follow behind adapter boundaries. Prefer native, explicit, testable machinery over a browser runtime or a pile of always-awake helpers.
