# Interfayce — Build Status

Updated: 2026-08-04

## Current checkpoint

- Branch: `codex/desktop-surfaces`
- Draft PR: https://github.com/lag0matic/Interfayce/pull/1
- Checkpoint scope: feature-complete personal build and first installer
- Native host: `native/build/bin/InterfayceOverlay.exe`
- Native audio engine: `native/build/bin/InterfayceAudioEngine.exe`
- Python suite: 61 passing tests

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
- Music uses circular icon controls; the decorative static playback meter was removed.
- Music is the startup deck. Its media-session query refreshes asynchronously so helper startup cannot block wrist interaction; the last known state remains visible while it refreshes.
- Music status, artwork, and transport controls now reuse the resident localhost service. The old two-second `cmd.exe`/Python launch loop was removed because it caused Windows application-start cursor feedback.
- Playspace shows only baseline/adjusted session state and an abstract origin-reset glyph.
- A compact gear opens wrist settings. The first live controls are persistent Kokoro output volume and mute; they update the voice service immediately.
- The wrist Settings deck can open a single-instance desktop configuration window on demand. It selects the shared Music/Comms microphone, mirrors TTS volume and mute, and controls Interfayce haptic strength; the window never opens automatically.
- Spotify OAuth uses Authorization Code with PKCE through the existing Covasify developer app and `http://127.0.0.1:8888/callback`. Live authorization, protected-token reload, account lookup, and Web API search all pass.
- Music keeps explicit transport commands on the local fast path and forwards only unrecognized requests to a constrained DeepInfra intent router. The provider key and Spotify tokens are protected with Windows DPAPI.
- Music transport glyphs follow their actions: previous is `|<`, next is `>|`, and the center control shows pause while Windows reports playback and play while paused.
- Conversational play requests canonicalize artist names through Spotify, then require confident title and artist matches before playback. Ambiguous or unrelated search results fail closed.
- Spoken Music controls now cover search/play, transport, status, Spotify volume, mute, and unmute. Relative volume requests accept natural phrasing and explicit percentage-point steps, while absolute and resulting values remain bounded to 0–100. Both successes and safe command failures receive short asynchronous Kokoro acknowledgments.
- The Music response line has its own full-width strip at the bottom of the deck, clear of the transport controls.
- Comms is a deliberately separate wrist deck with one continuous-dictation mic toggle and one clear-chatbox control. Each completed local Parakeet phrase is sent immediately through VRChat OSC, while the latest transcript and capture state remain visible on the wrist.
- Comms and Music share an explicit capture lock, so dictation can never be mistaken for a Spotify command or steal an already-active microphone session. Chatbox messages are bounded to VRChat's 144-character limit.
- Four configurable Comms shortcuts send bounded canned OSC chatbox messages directly from the wrist. Blank slots remain inert; labels and messages are edited in the desktop Settings window.
- Selected desktop and keyboard surfaces use subtle violet backlight rather than thick grab bars. Their functional grab regions are visually quiet.
- The wrist remains mounted to the left inner wrist using the live-fitted transform in `InnerLeftWristTransform()`. A gaze-and-presentation gate now gives it a fast fade-in, slower fade-out, and hysteresis so it disappears out of view without flickering or stealing hidden clicks.
- Desktop source pickers show native Windows application icons. Their application pages are pre-rendered and switched as persistent GPU textures, avoiding shared-texture hover flicker and runtime page blanks.
- Current Windows rows include a reuse action that stops only Interfayce's capture and returns the existing VR surface to a freshly populated picker while preserving its session position and size.
- Current Windows rows now include per-surface locks. Locked surfaces remain fully interactive but ignore one-hand movement and two-hand scaling until unlocked.
- The grouped-target recovery control brings every active desktop surface into an eye-level arrangement and places the independent keyboard below and slightly closer. Recovery deliberately includes locked surfaces.
- The Rig deck is now a filled feminine cybernetic scanner rather than a grid of text boxes. Native battery nodes and percentages remain live above the bundled visual asset, with external leader lines for chest and hip readings.
- The persistent header shows the lowest connected battery percentage. Controller readings refresh natively; SlimeVR readings refresh asynchronously without blocking deck changes.
- Battery alerts speak through the existing Kokoro queue on low (20%) and critical (10%) threshold crossings. Each crossing is announced once, simultaneous alerts are combined, and a recovered device can arm a future warning again.

## Startup capability gates

- At startup, Interfayce checks SlimeVR on `127.0.0.1:21110`. If unavailable, Slime tracker/reset controls are omitted for that play session instead of allowing the Rig tab to block.
- Spotify presence is checked natively through the running process list. Opening Music does not launch a slow helper when Spotify is absent.
- `--service-status` reports both checks without initializing SteamVR.
- `--desktop-sources` inventories capturable sources without SteamVR.
- `--desktop-capture-probe` verifies receipt of a real GPU frame without SteamVR.
- `--shutdown` asks a running host to exit through its normal session-baseline cleanup path.

## Audio routing

- The first-party user-mode audio engine is now a separate native executable rather than part of the SteamVR overlay host.
- `InterfayceAudioEngine.exe --probe-spotify 5` finds Spotify's root process and captures its complete process tree through Windows process-loopback WASAPI at 48 kHz, stereo, 16-bit PCM.
- Live positive control captured 239,520 frames in five seconds, approximately 99% audibly active, with zero discontinuities.
- Live negative control targeted Explorer while Spotify continued rendering. It captured the same real-time frame cadence but 0% audible frames, demonstrating that unrelated desktop audio did not leak into the target stream.
- Spotify continued playing through its existing physical output throughout both tests; the probe changes no endpoints or system routing.
- Windows SDK/WDK 10.0.28000, the Visual Studio 2026 driver integration, Inf2Cat, StampInf, SignTool, and DevCon are installed. A clean x64 build of Microsoft's stock SysVAD kernel driver completed WDK validation and produced a test-signed `.sys`; nothing was installed or loaded.
- The experimental Interfayce virtual-audio package built, validated, and passed an end-to-end development test, but Microsoft production-signing economics made it inappropriate for this personal deployment.
- The test driver, Driver Store package, and development certificates were completely removed. Every Windows loader and hibernation-resume entry has test-signing disabled, and the machine returned to a clean normal boot.
- Production-signed VB-CABLE now supplies only the persistent playback/capture endpoint pair. `InterfayceAudioEngine.exe --broadcast-spotify` connects the proven process-specific capture path to the exact `CABLE Input (VB-Audio Virtual Cable)` endpoint and fails closed rather than falling back to a physical output.
- A live VB-CABLE proof delivered 240,000 virtual-microphone frames over five seconds with 99.04% active Spotify audio while the eight-second process capture reported zero discontinuities. With the engine stopped, a two-second cable capture had zero samples above the audible threshold.
- Music now has a separate orbital broadcast control, off by default, with starting/live/failure/off status. It owns the isolated engine through a kill-on-close Windows job and a graceful stop event; overlay exit or failure returns VB-CABLE to silence.
- `--broadcast-controller-probe` passed offline: the engine reached `BROADCAST LIVE`, accepted a graceful stop, and left no child process behind.
- The wrist broadcast gate passed live VRChat testing with `CABLE Output (VB-Audio Virtual Cable)` selected. Initial low level was traced to the ordinary input-volume control; after correction, measured cable output matched the raw Spotify capture and VRChat received audio.
- Broadcast boost is persistent and adjustable from 0 through 24 dB in 3 dB steps. The audio engine applies the gain only to the VB-CABLE route and uses a soft limiter; normal Spotify listening volume is untouched.

## Settings and packaging

- The desktop settings window now owns General and Integrations tabs: input/output devices, TTS level/mute/speed, haptics, broadcast gain, Kokoro endpoint/model/voice, Spotify OAuth, and the constrained LLM provider.
- LLM fallback has an enforced enable toggle. Fresh installs are disabled and blank; when disabled, the client does not read a key or contact a network endpoint.
- Spotify tokens and the LLM key remain DPAPI-protected. No personal endpoint, client ID, device name, settings JSON, or credential blob is embedded in the source or installer payload.
- Spotify song announcements now run inside the resident service lifecycle instead of depending on an orphanable `spotify-watch` process.
- The selected orbital-aperture identity ships as SVG, 1024 px PNG, multi-resolution ICO, native executable resources, shortcuts, and installer branding.
- `packaging/build-installer.ps1` produces a self-contained, per-user Inno Setup package. It bundles the static-runtime native host, PyInstaller voice service, pinned SolarXR protocol commit, and isolated Node runtime.
- The staged support service passed local health/settings startup and graceful-shutdown checks. The 236.2 MB payload compresses to a 73.6 MB installer and contains no personal literals or private configuration files.

## Constraints to preserve

- `OverlayRenderer` retains one D3D11/DXGI shared texture and repaints it in place. Do not recreate GPU devices or overlay textures for ordinary panel updates.
- Keep `InnerLeftWristTransform()` unchanged unless David explicitly starts another live fitting pass.
- Right trigger is UI click. Index B double-tap/hold is playspace drag only.
- Playspace movement may write only SteamVR's temporary working standing origin. Reset and normal shutdown restore the immutable startup snapshot; never call `CommitWorkingCopy`, infer a center from the HMD, or add floor/recenter controls without an explicit new decision.
- Closing a VR desktop surface must never close its source application.
- Do not reintroduce a magnetic target halo, timed surface handoff, or visual laser without a new live interaction reason.
- SlimeVR Node adapters are development-only and currently rely on a temporary reference checkout; vendor and version-pin the SolarXR protocol before packaging.

## Larger features intentionally deferred

1. Configurable handedness and wrist-panel positioning.
2. Clipboard paste and other VR text-entry conveniences.
3. First-run diagnostics plus installed version and update support.
4. Personal favorite-application shortcuts for one-action desktop surface spawning.
5. Active/glanceable/sleeping capture update policies.
6. Additional diagnostics or visual polish only when live use identifies a concrete need.
7. Final UI unification pass: consistent corner radii, spacing, control silhouettes,
    selection glow, and interaction feedback across every wrist deck and desktop surface.

The intended next expansion is configurable handedness and wrist-panel positioning, followed by
clipboard conveniences. Battery/status handling is implemented; its low/critical behavior is
covered by deterministic tests and awaits a natural depleted-battery playtest.

## Verification record

- Native Release build succeeds with SteamVR stopped.
- The isolated audio engine builds and captures Spotify-only PCM with a clean unrelated-process negative control.
- `--service-status` is the intended offline capability smoke test.
- Windows Graphics Capture previously returned a real display frame through `--desktop-capture-probe`.
- Picker selection and paging, application icons, surface reuse, display/app capture, pointer clicks, vertical scrolling, ambidextrous typing, key feedback, one-hand movement, two-hand resizing, wrist recovery controls, and wrist visibility fading have passed live VR testing.
- Per-surface movement locks and grouped Bring All recovery, including independent keyboard placement, have passed live VR testing.
- Spotify OAuth, conversational Music requests, generic artist-name correction, fail-closed track selection, Spotify volume control, and spoken success/failure responses have passed live testing.
- Comms mic toggle, continuous phrase transcription, OSC delivery, clear pulse, transcript display, and capture isolation have passed live VR testing.
- Desktop microphone selection, TTS wrist mirroring, haptic strength, on-demand single-instance launch, and state-aware Music transport glyphs have passed live testing.
- Battery location/readability and the redesigned Rig scanner passed a live headset check.
- Battery low/critical threshold crossings, deduplication, combined speech, and recovery
  re-arming pass deterministic tests; a naturally depleted-device playtest remains outstanding.
- Python proof-of-concept suite: 64 passing checks.

## North star

Interfayce is David/Lag0Matic's personal, low-overhead VRChat cockpit, not a general dashboard product. Windows and SteamVR/OpenVR come first; Linux/OpenXR can follow behind adapter boundaries. Prefer native, explicit, testable machinery over a browser runtime or a pile of always-awake helpers.
