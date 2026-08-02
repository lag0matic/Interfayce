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

Before any transform write, Interfayce reads and snapshots SteamVR's working standing-origin matrix through `VRChaperoneSetup`. That immutable session baseline is the prerequisite for safe drag, undo, floor adjustments, and restore-session-baseline behavior.

## Resource budget

Every captured desktop window needs an explicit update policy:

| Policy | Intended use | Update behavior |
|---|---|---|
| Active | currently interacting with a window | 30–60 fps as needed |
| Glanceable | chat, media, simple telemetry | 5–10 fps |
| Sleeping | dismissed / out of sight window | frozen texture or no capture |
| Adaptive | optional policy | backs off as GPU frametime worsens |

The persistent cockpit should not depend on desktop capture or an embedded browser runtime.

## Wrist cockpit and utility deck

The wrist overlay mounts on the **inner wrist**, in a natural palm-up watch-check pose. It should be thin and semi-transparent, with user-adjustable wrist offset, angle, scale, and optional arm selection.

Tracker health is passive status, not the purpose of the entire lower panel. The primary lower-panel control is a multi-purpose **utility deck**:

- shows a compact live battery board for 8 logical SlimeVR body slots (elbows, chest, hip, thighs, and combined feet) plus both Index controllers;
- expands an individual device into its connection state, percentage, and relevant recovery/control actions;
- opens focused cockpit controls (playspace, floor, SlimeVR recovery, audio);
- launches personal desktop overlay windows such as weather, email, Discord, or browser;
- spawns each requested window at a comfortable eye-line distance in front of the user;
- makes a spawned window immediately grabbable and moveable, with a remembered position available later;
- lets dismissed windows sleep/freeze rather than continuing capture.

The panel uses one persistent top control strip—**Music, Desktop, Playspace, Rig/Batteries**—and swaps a single lower deck beneath it. This keeps controls one gesture away without turning the inner wrist into a dashboard graveyard. Render only the selected deck; inactive pages have no capture/update work beyond their own necessary data feeds.

### Wrist visibility gate

Keep the panel hidden/sleeping until both are plausibly true:

1. the configured wrist/controller is in a palm-up, inner-wrist presentation pose; and
2. the panel's facing direction is inside a comfortable HMD gaze cone.

Use different show/hide thresholds plus a brief fade (rather than a single threshold) so normal hand motion does not cause popping. Once hidden, stop submitting texture changes unless the selected deck's state becomes dirty. The visibility gate is presentation-only: controller action polling and safe playspace movement must remain available regardless of whether the panel is visible.

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

Interfayce's first native drag should therefore use the same rising-edge/per-frame-delta model, a valid-pose guard, a much tighter personal hard bound, working-set preview while held, one explicit commit on release, plus session-baseline restore and undo. Keep the native transform path entirely separate from the eventual wrist UI.

The wrist utility deck will include a hold-to-confirm **Restore session baseline** control. It calls the same exact-baseline restore path used when Interfayce exits. It must not use HMD position, "recenter", or any inferred transform; the only safe default is the immutable session baseline captured before Interfayce moved anything.

The project now includes a minimal, unregistered Index action manifest and default binding under `assets/steamvr/`. It maps only B-button `double` to left/right drag actions and deliberately leaves a normal B click alone. Registering an app/action manifest with SteamVR and opening a binding test is a separate, user-visible step; do not do that silently.

### Floor reset: keep the good part, improve the fragile part

OVRAS's floor fix is a small measurement workflow:

1. Put a controller on the floor and start the operation.
2. Validate that both controllers are tracked; use the lower one as reference.
3. Sample 25 frames to settle orientation/pose noise.
4. Apply a Y-axis origin correction; its separate "recenter" variation also applies X/Z.
5. Offer undo, then reset transient offsets.

For Index controllers, OVRAS applies fixed hardware/orientation correction values. Interfayce should keep the validation, multi-frame sampling, explicit success/failure feedback, and undo—but derive or calibrate any Beyond/Index-specific correction from David's actual setup rather than inheriting magic constants.

Initial Interfayce floor-control scope:

- **Set floor:** controller-on-floor, stable multi-frame measurement, Y only.
- **Set floor + center:** same measurement plus X/Z recentering.
- **Undo last floor operation:** one clearly available reversal.
- **Restore session baseline:** guarded escape hatch for a bad experiment.

Defer chaperone-profile editing, gravity, redirected walking, and automatic boundary behaviors until the core transforms are proven reliable.

### David's initial Index floor-reference calibration

Use the right or left Index controller placed **controls-up** as the canonical floor pose, matching David's existing habit. Live read-only samples on 2026-08-02 were stable to 0.1 mm across 25 frames. The tracked controller origin was approximately **3.7 cm below** the standing-floor plane in the controls-up pose; the back-down pose was approximately 4.5 cm below it. The 8 mm orientation difference is real but small.

This is a personal calibration reference, not a universal Index magic constant. The actual floor operation must re-sample, require a stable pose, show a preview, and offer a single-operation undo before committing any SteamVR origin change.

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

## Voice control

Preferred initial pipeline:

```text
Music mic button → local microphone capture → local STT
    → deterministic fast path for obvious commands
    → constrained LLM fallback for conversational requests
    → validated Spotify/Interfayce action
    → short acknowledgment through David's local TTS server
```

The LLM returns a constrained intent and arguments; it does not receive arbitrary authority to operate the computer. Simple commands such as pause, next, and volume changes bypass it for speed. Natural requests and conversational follow-ups may use a local model or chosen API. Track metadata, captured application text, and other untrusted strings are data, never instructions.

VRChat textbox dictation is a separate Comms-deck path:

```text
Comms mic button → local microphone capture → local STT → preview / cancel / send → VRChat OSC chatbox
```

Command mode and dictation mode must have distinct visible states and must never silently cross-route. Neither mode may enable the VRChat voice microphone. Microphone capture is always deliberately armed and time-bounded.

### Kokoro acknowledgment server

David's existing Kokoro server exposes an OpenAI-style TTS API on the separate Arkive machine. The port must remain configurable. David's latest recollection is port `5000`; an older note said `7079`, but neither port answered a health probe while the service was offline on 2026-08-02. Verify against the running server before choosing the local default.

```text
POST http://thearkive.local:<configured-port>/v1/audio/speech
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

The base URL, model, voice/blend, response format, speed, and timeout belong in ignored local configuration. TTS is acknowledgment, not the authority that decides whether an action succeeded. A failed or sleeping TTS server must not roll back, block, or misreport a completed Spotify action; the wrist should still show the result visually. Keep acknowledgments short, bound request timeouts, and discard stale queued speech when a newer command supersedes it.

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

For personal development on 64-bit Windows, driver signing/testing is a real prerequisite: test-signed kernel drivers require elevated setup and typically test-signing mode plus a restart. Do not change boot security settings or install a driver without David's explicit approval and a recovery plan.

Linux is deferred; PipeWire offers a later native equivalent, but it does not constrain the first Windows implementation.

Risks to design around:

- feedback loops;
- accidentally routing all desktop audio rather than only Spotify;
- accidental music broadcast;
- platform/copyright rules when playing music for other people.

## Licensing boundary

OVR Advanced Settings is GPL-3.0 and may be studied or used as a private-use foundation, with distribution implications if the project is later shared. OVR Toolkit is proprietary: study behavior and user experience, but do not reuse code or assets.
