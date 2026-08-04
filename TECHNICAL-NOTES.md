# Personal VRChat Cockpit — Technical Notes

## Proposed shape

Keep the project as a small shared core with runtime-specific adapters:

```text
Shared core
├─ profiles and user state
├─ controller / gesture state machine
├─ media-session integration
├─ VRChat OSC client
├─ tracker-status / SlimeVR bridge
├─ performance and resource budgeter
└─ overlay state

Runtime adapters
├─ SteamVR / OpenVR first
└─ OpenXR later
```

SteamVR/OpenVR is the first practical target. An adapter boundary keeps Linux/OpenXR support from requiring a total rewrite later.

### SteamVR controller telemetry

The first SteamVR adapter uses OpenVR's left/right controller roles and the `Prop_DeviceBatteryPercentage_Float` / `Prop_DeviceIsCharging_Bool` properties. It feeds the Index controller readings into the same rig battery model as SlimeVR, but remains separate from the eventual native overlay/playspace implementation.

Before any transform write, Interfayce reads and snapshots SteamVR's working standing-origin matrix through `VRChaperoneSetup`. That immutable session baseline is the prerequisite for safe drag and restore-session-baseline behavior.

## Resource budget

Every captured desktop window needs an explicit update policy:

| Policy | Intended use | Update behavior |
|---|---|---|
| Active | currently interacting with a window | 30–60 fps as needed |
| Glanceable | chat, media, simple telemetry | 5–10 fps |
| Sleeping | dismissed / out of sight window | frozen texture or no capture |
| Adaptive | optional policy | backs off as GPU frametime worsens |

The persistent cockpit should not depend on desktop capture or an embedded browser runtime.

### Desktop interaction decisions

The live-tested desktop baseline uses independent OpenVR overlays backed by Windows Graphics Capture. A spawned surface begins as its own display/application picker, then becomes the selected capture. Closing it destroys only the Interfayce overlay. A wrist list supplies Bring to me and Close recovery actions.

Interaction is intentionally geometric rather than magnetic. Each hand casts its controller-tip ray; the nearest actual surface intersection wins. The only visible guide is a small dot at that intersection. Earlier laser beams, oversized keyboard occlusion gutters, and timed target handoffs made nearby keyboard/monitor arrangements feel as though one surface was stealing the pointer, so they were removed. Do not reintroduce them without a specific live-tested need.

Use deliberate Index force-grip thresholds for movement so resting fingers do not grab surfaces while typing. One-hand grip preserves the grab offset; two-hand grip scales around the controller midpoint. Keyboard and captured surfaces use the same session-only placement model. Position and size persistence is explicitly unwanted because the useful layout changes between sessions.

Application picker rows use icons extracted from the owning Windows executable. Picker content is immutable while aiming—the controller dot supplies hover feedback—because repainting a compositor-shared texture for every ray transition produced visible cleared frames. Multi-page application lists are pre-rendered into persistent picker textures and page turns switch handles, rather than repainting or allocating at click time. A captured surface can be returned to a freshly inventoried picker without moving/resizing it or closing its source application.

At host startup, capability checks must be bounded and native. SlimeVR is probed once at `127.0.0.1:21110`; if absent, its trackers and reset controls stay unavailable for the play session. Spotify presence is read from the process list before invoking any slower media helper. The `--service-status` diagnostic performs these checks without initializing SteamVR.

## Wrist cockpit and utility deck

The wrist overlay mounts on the **inner wrist**, in a natural palm-up watch-check pose. It should be thin and semi-transparent, with user-adjustable wrist offset, angle, scale, and optional arm selection.

Tracker health is passive status, not the purpose of the entire lower panel. The primary lower-panel control is a multi-purpose **utility deck**:

- shows a compact live battery board for 8 logical SlimeVR body slots (elbows, chest, hip, thighs, and combined feet) plus both Index controllers;
- expands an individual device into its connection state, percentage, and relevant recovery/control actions;
- opens focused cockpit controls (playspace, floor, SlimeVR recovery, audio);
- launches personal desktop overlay windows such as weather, email, Discord, or browser;
- spawns each requested window at a comfortable eye-line distance in front of the user;
- makes a spawned window immediately grabbable, moveable, and resizable for the current session; transforms deliberately do not persist because the useful arrangement varies day to day;
- lets dismissed windows sleep/freeze rather than continuing capture.

The panel uses one persistent top control strip—**Music, Desktop, Playspace, Rig/Batteries**—and swaps a single lower deck beneath it. This keeps controls one gesture away without turning the inner wrist into a dashboard graveyard. Render only the selected deck; inactive pages have no capture/update work beyond their own necessary data feeds.

### Wrist visibility gate

Keep the panel hidden/sleeping until both are plausibly true:

1. the configured wrist/controller is in a palm-up, inner-wrist presentation pose; and
2. the panel's facing direction is inside a comfortable HMD gaze cone.

Use different show/hide thresholds plus a brief fade (rather than a single threshold) so normal hand motion does not cause popping. Once hidden, stop submitting texture changes unless the selected deck's state becomes dirty. The visibility gate is presentation-only: controller action polling and safe playspace movement must remain available regardless of whether the panel is visible.

The implemented first pass combines HMD gaze alignment, panel facing, and distance with separate enter/exit thresholds. It fades in quickly and out more slowly; wrist hit testing is disabled once the panel is mostly transparent. Live testing found the behavior natural without changing the fitted `InnerLeftWristTransform()`.

### Battery time remaining

Show an estimated **hours left** only after accumulating enough local, per-device discharge history. Estimate from the recent battery percentage slope while the device is actually discharging; ignore charging, reconnect jumps, and stale readings. Until the estimate has confidence, show only the actual percentage. The displayed estimate should be rounded/coarse (for example `~2h 15m`), with a clear low-battery warning based on the earliest likely depletion across the body rig—not false `2h 13m 47s` precision.

When SlimeVR itself supplies a runtime estimate, prefer it over Interfayce's local estimate. Each combined foot slot represents its ankle and foot tracker together: show the lower reported percentage/earlier runtime and flag the logical foot unhealthy if either constituent disconnects.

## Playspace drag

Required state machine:

```text
idle → deliberate arming gesture → hold same control → drag → release / commit
```

Design constraints:

- Match David's existing OVR Advanced Settings muscle memory as closely as practical after we inspect/configure its actual binding.
- No motion from a single accidental press.
- Add subtle haptic feedback when armed and on release.
- Show a minimal axis/ghost marker while dragging.
- Keep undo/reset nearby but separately guarded.
- Keep gestures configurable per controller/input profile for future gloves and OpenXR hardware.

### OVR Advanced Settings reference findings

OVRAS implements temporary playspace movement by changing SteamVR's **working standing/seated origin transforms** through `VRChaperoneSetup`, then showing the working-set preview while moving. It maintains an original transform for reset, applies controller-pose deltas to a local offset, and has hard out-of-range guards that reset and restore an autosaved chaperone profile rather than letting a broken pose become permanent.

Its default Knuckles binding is the muscle-memory pattern David described:

```text
B click          → space turn / hand-swap behavior
B double-click   → override space drag
hold second press → keep dragging
release          → finish drag
```

The override action wins over normal actions, which is how a double-click can safely share the same physical button. Interfayce should preserve that intention, while using its own explicit input state machine rather than blindly copying implementation details.

The first pure input state machine is now in `src/interfayce/gesture.py`: a lone press can never start a drag; a second press inside the configured double-tap window starts drag; releasing that held second press ends it. SteamVR bindings and haptics will sit outside this core so the safety behavior stays testable and can later be reused for gloves/OpenXR.

The transform calculations likewise live in `src/interfayce/transforms.py`, independent of OpenVR. A drag creates a proposed translation from a captured controller pose and the immutable session baseline; only the SteamVR adapter will eventually be allowed to apply that draft, after safety checks.

### OVRAS drag-loop comparison — behavior to preserve

OVRAS's good feel comes from a few concrete mechanics, verified against its current source:

- It chooses the active hand on a drag action's rising edge and releases/hands off deterministically if another eligible drag action is still held.
- Each update reads the active controller's valid `TrackingUniverseStanding` pose (or seated pose when it has explicitly detected seated mode).
- It applies **per-frame controller deltas**, not a repeatedly recomputed absolute destination. That avoids a visible jump when a drag starts and naturally follows the hand.
- It converts controller coordinates into its unrotated offset frame before calculating the delta, so dragging stays intuitive after a playspace yaw offset.
- It respects axis locks and a drag multiplier; Interfayce v1 should default to 1× and have no hidden smoothing.
- It rejects invalid/disconnected poses and treats a nonsensical one-frame movement over 100 m as a tracking glitch, resetting rather than committing a disaster.
- Each live movement writes only the SteamVR **working** standing origin and calls `ShowWorkingSetPreview`; it does not commit the working copy every frame.
- OVRAS has optional frame skipping for comfort, gravity, collision-bound handling, profiles, and turn mechanics. Those are not required for Interfayce's first drag implementation.

Interfayce's native drag uses the same rising-edge/per-frame-delta model, a valid-pose guard, a tight personal hard bound, and working-set preview while held. Release keeps the temporary working transform for the Interfayce session; it never commits SteamVR's working copy. Keep the native transform path entirely separate from the wrist UI.

The wrist utility deck will include a hold-to-confirm **Restore session baseline** control. It calls the same exact-baseline restore path used when Interfayce exits. It must not use HMD position, "recenter", or any inferred transform; the only safe default is the immutable session baseline captured before Interfayce moved anything.

The project now includes a minimal, unregistered Index action manifest and default binding under `assets/steamvr/`. It maps only B-button `double` to left/right drag actions and deliberately leaves a normal B click alone. Registering an app/action manifest with SteamVR and opening a binding test is a separate, user-visible step; do not do that silently.

### No floor or HMD recenter controls

Floor calibration and recentering are explicitly out of scope. Interfayce treats SteamVR's persisted room setup as truth and must never replace it with a center inferred from the headset. The Playspace control restores the exact immutable transform captured at Interfayce startup; it is not a generic recenter or factory reset.

Session drag writes only the temporary working standing-origin transform and never calls `CommitWorkingCopy`. Normal shutdown restores the startup snapshot. If the process crashes before restoration, restarting SteamVR remains the reliable recovery path because Interfayce has not overwritten the persisted room configuration. A small watchdog may later improve crash recovery without broadening transform authority.

## SlimeVR

The app is not a replacement SlimeVR server.

### Keep

- tracker connection and battery state;
- an obvious route into full reset + body-mount calibration;
- diagnostics when requested.

### Do not duplicate

- drift reset: handled by the physical chest tracker double tap;
- automatic inference of whether David is standing, dancing, lying down, or cuddled up.

The recovery UI must distinguish two real scenarios:

```text
Upright / soldier pose → drift reset (handled by tracker itself)
Left room / remounted / completely fucked → full reset + body-mount calibration
```

Open question: determine the cleanest supported local SlimeVR-server integration for invoking full calibration. If that is not a stable API, the app should deep-link/launch the appropriate SlimeVR UI rather than inventing a brittle workaround.

### Live data-feed integration

SlimeVR Server exposes a local binary WebSocket API at `ws://127.0.0.1:21110`. Its GUI uses the SolarXR FlatBuffers protocol to subscribe to a `DataFeed` containing device hardware status and tracker assignment/status data. The useful device fields include battery percentage, charging/runtime state where supported, and tracker role/status.

Protocol schemas evolve with SlimeVR Server. Interfayce must version-pin the SolarXR schema to the installed server build or provide a compatibility adapter; never assume the repository head schema can decode an older/newer local server correctly. During initial live testing on David's machine, the server was reachable and reported protocol version 22, while the current public schema returned empty bundles—treated as a schema mismatch, not as no tracker data.

### Reset-control design (validated against GUI source)

SlimeVR's own GUI uses the SolarXR `ResetRequest` RPC for a `Full` reset and a subsequent `Mounting` reset. The server publishes guard flags that determine when mounting reset is allowed; Interfayce must display and obey those flags rather than guessing timing. The wrist flow will require an intentional hold/confirm sequence, show server-reported reset progress, and will never duplicate drift reset (David's chest-tracker double tap owns that workflow).

### Live adapter note

`tools/slimevr_probe.cjs` now decodes the matching SolarXR FlatBuffers data feed from David's running server: protocol version 22, ten physical trackers, body assignments, tracker status, and battery percentages. It is development-only until the exact protocol/runtime is vendored and version-pinned into Interfayce.

## VRChat OSC: song announcement

VRChat receives OSC on UDP port 9000 by default (configurable) and supports:

```text
/chatbox/input (string text, bool sendImmediately, bool notificationSfx)
/chatbox/typing (bool)
```

The desired announcement is one immediate, silent message per genuine track change:

```text
♫ Artist — Title
```

Rules:

- deduplicate by track identity;
- wait briefly before sending, to filter rapid skips;
- respect the 144-character VRChat chatbox limit;
- truncate cleanly when necessary;
- do not emit a continuous now-playing status;
- do not activate the typing indicator;
- leave the feature opt-in/toggleable.

After a song announcement, the current implementation sends an **experimental** empty `/chatbox/input` payload after seven seconds to clear the displayed chatbox. VRChat does not document a dedicated OSC clear endpoint, so this must remain clearly marked and live-tested across VRChat updates.

Source: [VRChat OSC as Input Controller](https://docs.vrchat.com/docs/osc-as-input-controller), [VRChat OSC overview](https://docs.vrchat.com/docs/osc-overview).

## Spotify/media integration

Use the operating system's media-session API to learn the current track and issue immediate transport commands. On Windows, this is now implemented through Global Media Transport Controls and has a plausible Linux counterpart through MPRIS.

Spotify OAuth/Web API access is explicitly in scope for search, play-by-name, conversational selection, and other behaviors media sessions cannot satisfy. Credentials and refresh tokens belong only in ignored local configuration. Keep the media-session path for fast basic transport and as a useful fallback.

The implemented desktop authorization uses Spotify's Authorization Code with PKCE flow and the existing Covasify developer application. Its registered callback is the explicit loopback URI `http://127.0.0.1:8888/callback`; `localhost` must not replace that literal address. Interfayce requests only playback read/write, current-track, private-playlist read, and library-read scopes. It never needs or stores the Spotify client secret.

The client ID is a non-secret preference in `%LOCALAPPDATA%\Interfayce\settings.json`. Access and refresh tokens are serialized into a Windows DPAPI-protected blob at `%LOCALAPPDATA%\Interfayce\secure\spotify-oauth-token.dpapi`, bound to David's Windows account. The API client refreshes shortly before expiry and retains the existing refresh token when Spotify omits a replacement from a refresh response. Live verification on 2026-08-03 authenticated David Armstrong and returned real Web API artist search results.

Spotify's February 2026 development-mode API changes removed the artist top-tracks endpoint used by older integrations. Interfayce therefore resolves an artist to Spotify's canonical name, searches tracks using that identity, and validates both title and credited artist before starting playback. Spoken or STT-mangled artist names are handled generically rather than through artist-specific aliases, and low-confidence results fail closed.

## Voice control

Preferred initial pipeline:

```text
Music mic button → local microphone capture → local STT
    → deterministic fast path for obvious commands
    → constrained LLM fallback for conversational requests
    → validated Spotify/Interfayce action
    → short acknowledgment through David's local TTS server
```

The LLM returns a constrained intent and arguments; it does not receive arbitrary authority to operate the computer. Explicit commands such as pause and next bypass it for speed. Unrecognized natural requests, including volume phrasing, go through the constrained router and are locally validated before execution. Track metadata, captured application text, and other untrusted strings are data, never instructions.

The current router uses a user-configured OpenAI-compatible chat-completions endpoint, model, and temperature. It may emit only the enumerated Music actions for playback search, transport, status, volume, mute, or no action. The provider URL and model are non-secret local preferences; the API key is stored as a Windows DPAPI-protected blob at `%LOCALAPPDATA%\Interfayce\secure\llm-api-key.dpapi` and must never enter the settings JSON, logs, wrist UI, or repository. Fresh installations leave the provider blank and LLM fallback disabled.

VRChat textbox dictation is implemented as a separate Comms-deck path:

```text
Comms mic toggle → repeated bounded microphone capture → local STT → immediate VRChat OSC chatbox send
```

Command mode and dictation mode must have distinct visible states and must never silently cross-route. Neither mode may enable the VRChat voice microphone. Microphone capture is always deliberately armed and time-bounded.

The live Comms deck intentionally has only two actions: toggle continuous phrase dictation and send the existing empty-message chatbox clear pulse. Completed Parakeet utterances are capped at 144 characters and sent immediately; the most recent text and capture state remain visible on the wrist. Music and Comms share a non-blocking capture lock, so one mode refuses to start while the other owns the microphone.

### Kokoro acknowledgment server

David's existing Kokoro server exposes an OpenAI-style TTS API on a separate LAN machine. Its complete speech endpoint is stored only in the local settings profile; fresh installations leave it blank. Direct IPv4 was materially faster than local-name resolution during testing, but neither address belongs in source or packaged defaults:

```text
POST {configured Kokoro speech endpoint}
```

Example request:

```json
{
  "model": "tts-1",
  "input": "Playback started.",
  "voice": "af_heart:35,jf_alpha:20,bf_emma:45",
  "response_format": "wav",
  "speed": 1.0
}
```

Supported response formats include `wav`, `mp3`, `opus`, `aac`, `flac`, and headerless `pcm`. Use WAV for the first Interfayce acknowledgment path because it is self-describing and easy to play safely; reserve PCM for a later streaming path where reduced startup latency materially helps. Speed is configurable from `0.25` through `4.0`, and `voice` may be one Kokoro voice or a weighted comma-separated blend.

Discovery/health endpoints:

- `GET /health`
- `GET /v1/models`
- `GET /voices`
- `GET /v1/voices`

The confirmed health response from the configured server's `/health` route is:

```json
{"service":"Kokoro TTS Server (OpenAI Compatible)","status":"ok"}
```

A live WAV request using `af_heart` completed in about 11.1 seconds and produced a valid 208,844-byte file. That timing may include server/model warmup, but it confirms that synthesis must run asynchronously. Show action completion on the wrist immediately, then play the spoken acknowledgment when it arrives. Do not hold the command UI in a busy state for the duration of TTS generation.

The base URL, model, voice/blend, response format, speed, and timeout belong in ignored local configuration. TTS is acknowledgment, not the authority that decides whether an action succeeded. A failed or sleeping TTS server must not roll back, block, or misreport a completed Spotify action; the wrist should still show the result visually. Keep acknowledgments short, bound request timeouts, and discard stale queued speech when a newer command supersedes it.

Speak both successful Music actions and completed command failures. In VR, hearing that a request was rejected or produced no confident Spotify match is substantially more ergonomic than requiring the user to inspect the wrist. Keep raw service/microphone diagnostics visual and logged rather than reading exception details aloud.

Non-secret runtime preferences are persisted in `%LOCALAPPDATA%\Interfayce\settings.json`. The first settings are TTS volume, mute, and speed; volume and mute are exposed through the wrist gear deck. Kokoro reloads them immediately before synthesis/playback so a wrist change applies without restarting the service. The desktop settings window owns endpoint/device selection and will grow into integration setup. OAuth refresh tokens and API keys must be stored with Windows credential protection and never written to this JSON file or rendered in VR.

The desktop settings window opens only through the wrist Settings deck's monitor/launch icon (or the explicit development command). A Windows named mutex makes it single-instance. It owns the shared Music/Comms microphone selection, TTS volume/mute, haptic amplitude, integration configuration, and a Diagnostics tab. Bounded, network-free local checks refresh whenever Settings opens and persist only non-secret status under `%LOCALAPPDATA%\Interfayce`; optional integrations are visually distinct from failures that need attention. Update discovery never runs in the background: the explicit button queries the latest GitHub release and can open its release page. Secrets continue to use DPAPI rather than JSON.

The native Music deck queries the resident localhost service for media state and artwork. Do not restore the old pattern of spawning `cmd.exe` and Python for each two-second refresh: Windows displayed recurring application-start cursor feedback even though the child windows used `CREATE_NO_WINDOW`.

## Installed runtime boundary

Development builds may still launch the support package through the repository's Python environment. Installed builds instead launch `service/InterfayceService.exe`, a windowless PyInstaller one-directory runtime containing only the libraries required for local STT, audio, Windows media sessions, OAuth, and settings. Optional SpeechRecognition cloud/Whisper/Torch integrations are explicitly excluded.

The native executables use the static MSVC runtime and resolve all writable artwork/cache output under `%LOCALAPPDATA%\Interfayce\cache`; installed files are never treated as writable state. The SteamVR manifest uses a path relative to its own installed directory. SlimeVR helpers use a pinned SolarXR protocol build and bundled Node executable rather than a developer checkout or a machine-specific path.

The Inno Setup package installs per-user under `%LOCALAPPDATA%\Programs\Interfayce`, requires no elevation, and deliberately preserves `%LOCALAPPDATA%\Interfayce` during uninstall/upgrade. The build stage is audited for known personal literals plus `settings.json` and `.dpapi` files before release.

`VERSION` is the release identity source consumed by CMake, the PyInstaller payload, and the installer build. The native tray, `--version`, Windows executable properties, Settings diagnostics, and Add/Remove Programs therefore expose the same version. GitHub update checks are user-initiated and do not download or execute an installer automatically.

## Audio routing

Interfayce will own its Windows music-to-VRChat route rather than depend on VAC or Voicemeeter. This is a real driver feature, not a light UI integration, so it must be isolated from the overlay host and built/tested as its own safety-critical component.

Windows design target:

```text
Spotify continues to physical headphones
          |
          | process-specific WASAPI loopback capture
          v
Interfayce user-mode broadcast engine (active only when gated on)
          |
          | low-latency PCM injection
          v
Interfayce virtual microphone endpoint -> selected as VRChat input
```

This keeps local listening independent, captures Spotify rather than all desktop audio, and makes the virtual microphone emit silence whenever the broadcast gate/VR session is off. Windows process-loopback capture supports targeting a specific process tree on Windows 10 build 20348 and later. A custom virtual capture endpoint still requires a kernel-mode audio driver; Microsoft SysVAD is the appropriate learning/reference architecture. The driver cannot be casually created and removed while VRChat is running, because that would invalidate VRChat's selected input device. Instead, install it once, keep it dormant, and only run the user-mode capture/injection path when explicitly enabled.

The first user-mode slice is implemented as the separate `InterfayceAudioEngine.exe`; WASAPI capture does not live in the SteamVR overlay process. Its offline Spotify probe resolves the root `Spotify.exe` process and includes the child tree, normalizing capture to 48 kHz stereo 16-bit PCM. A five-second live run delivered 239,520 frames with no discontinuities. A simultaneous isolation control against Explorer delivered the same frame cadence with zero audibly active frames while Spotify remained audible through the normal headphones. No default endpoint, application route, or SteamVR setting is modified.

The current machine now has matching Windows SDK/WDK 10.0.28000, Visual Studio Build Tools 2026 driver integration, Inf2Cat, StampInf, SignTool, and DevCon. Microsoft's stock x64 SysVAD kernel project builds, validates, and receives a local test signature when invoked through the 64-bit MSBuild host. The full sample solution's optional APO projects additionally depend on WIL, which Interfayce's minimal endpoint driver does not need. Do not interpret this verified build toolchain as approval to enable test-signing or install a driver.

Interfayce's minimal driver is derived from Microsoft's Simple Audio Sample and retains its Microsoft Sample License. It exposes fixed 48 kHz stereo signed-16-bit feed and microphone endpoints, published by Windows as `Speakers (Interfayce Virtual Audio)` and `Microphone Array (Interfayce Virtual Audio)`. Render frames enter a spinlock-protected, bounded half-second kernel ring; microphone reads drain it and zero-fill every underrun. The user-mode engine requires the exact Interfayce render identity and refuses to start if it is absent, so it cannot silently route captured Spotify audio to headphones or another physical device.

The custom driver passed a live simultaneous test, but it is retained only as an experimental reference because Microsoft production signing is disproportionate for this personal project. Its root device, Driver Store package, and development certificates were removed, and test-signing was disabled on both the normal Windows loader and its separate hibernation-resume loader. The deployed route uses production-signed VB-CABLE as the deliberately dumb endpoint pair: Interfayce writes only to exact MMDevice identity `CABLE Input (VB-Audio Virtual Cable)`, and VRChat selects `CABLE Output (VB-Audio Virtual Cable)`. Process selection, broadcast authority, fail-closed behavior, and silence gating remain owned by Interfayce rather than the cable driver. A live VB-CABLE proof delivered 240,000 frames over five seconds with 99.04% active Spotify audio and no process-capture discontinuities; with the engine stopped, a two-second output capture contained no samples above the audible threshold.

The Music deck owns broadcast as an explicit, session-only gate that always starts off. Its orbital transmitter control launches `InterfayceAudioEngine.exe` suspended, assigns it to a `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job, and only then resumes it. This makes an overlay crash close the job and kill the audio engine. Normal stop signals `Local\InterfayceBroadcastStop`, waits for process-loopback capture to exit, and retains forced termination only as a bounded fallback. Startup becomes visibly live only after the child survives its initialization window; an early or later exit becomes `BROADCAST FAILED`. The offline controller probe confirmed start, live state, graceful stop, and no lingering child.

The complete wrist-to-VRChat route passed live testing with `CABLE Output (VB-Audio Virtual Cable)` selected as VRChat's microphone. A misleading first low-level reading came from the ordinary input volume being turned down, not attenuation in process capture or VB-CABLE. After restoring that control, direct cable measurement peaked at roughly `0.034`, matching the raw Spotify capture, and VRChat received the broadcast.

For personal development on 64-bit Windows, driver signing/testing is a real prerequisite: test-signed kernel drivers require elevated setup and typically test-signing mode plus a restart. Do not change boot security settings or install a driver without David's explicit approval and a recovery plan.

Linux is deferred; PipeWire offers a later native equivalent, but it does not constrain the first Windows implementation.

Risks to design around:

- feedback loops;
- accidentally routing all desktop audio rather than only Spotify;
- accidental music broadcast;
- platform/copyright rules when playing music for other people.

## Licensing boundary

OVR Advanced Settings is GPL-3.0 and may be studied or used as a private-use foundation, with distribution implications if the project is later shared. OVR Toolkit is proprietary: study behavior and user experience, but do not reuse code or assets.
