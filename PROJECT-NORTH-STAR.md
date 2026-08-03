# Personal VRChat Cockpit — North Star

## What this is

A personal, low-overhead VR utility for David's everyday VRChat setup. It combines the *useful intent* of a playspace tool and a desktop-overlay tool without trying to become a generic replacement for either.

This is deliberately optimized for one real user and real habits. Generic distribution, broad hardware support, and feature-complete parity are not initial goals.

## The core promise

The app should make frequent VRChat actions immediate, intentional, and cheap:

- move the playspace without breaking ingrained muscle memory;
- show only the small amount of status worth seeing in VR;
- announce a Spotify track once through the VRChat chatbox;
- wake desktop windows only when actually needed;
- stay the hell out of the way otherwise.

Nothing should render, poll, capture, or consume meaningful resources merely because it could.

## Current hardware and use case

- Headset: Bigscreen Beyond 2E
- Face tracking: attached Babble FaceCam
- Hands: Valve Index controllers
- Body tracking: 10 SlimeVR trackers — chest, elbows, hip, thighs, ankles, and feet
- Main runtime today: SteamVR on Windows
- Later target: Linux, including Bigscreen Beyond 2E once NVIDIA support is no longer being a little bastard
- VRChat play style: mute / chatbox communication; this is intentional and aesthetic, not a limitation to "fix"

## Priority order

### V1: critical

1. **Guarded continuous playspace drag**
   - Preserve the familiar OVR Advanced Settings-style deliberate gesture as closely as practical.
   - The gesture must require intentional arming, then a hold-and-drag action.
   - A stray press must never move the playspace.
   - Movement must remain available when all desktop overlays are asleep.

2. **Spotify → VRChat OSC single-shot announcement**
   - On a genuine track change, send exactly one chatbox message:
     `♫ Artist — Title`
   - No permanent now-playing display and no repeated announcements.
   - Prefer a short stability delay before announcing so skips are not broadcast.
   - Do not use typing animation or the chatbox notification sound.

3. **Tiny wrist cockpit**
   - A compact rig battery board for 8 logical SlimeVR body slots (elbows, chest, hip, thighs, and combined feet) plus both Index controllers, with clear warnings in the quiet status layer.
   - Spotify play/pause, previous, next, volume, and current track at a glance.
   - Time and basic performance state (especially frametime).
   - It must be cheap enough to remain available; it is not a desktop dashboard.
   - A multi-purpose **utility deck** opens controls and spawns on-demand overlay windows; it is not a tracker-only page.
   - When enough local discharge history exists, show an estimated time remaining per device; otherwise show the plain battery percentage without pretending to know.

### Soon after V1

4. **On-demand desktop dock**
   - Personal app shortcuts such as weather, email, Discord, and browser.
   - Windows are captured only while useful; dismissed windows freeze or sleep.
   - Weather must be quickly reachable during an actual "tornado siren" moment.

5. **SlimeVR full recovery route**
   - Expose or launch the full reset + body-mount calibration workflow cleanly.
   - Do not recreate drift reset: it already belongs to the SlimeVR chest-tracker double tap.
   - Do not pretend one reset solves every tracking problem.

### Nice later

6. **Voice commands**
   - A microphone button on the Music deck deliberately starts and stops a command capture.
   - Use local Parakeet/Sherpa speech-to-text, a fast deterministic path for simple commands, and a small LLM router for natural or conversational requests.
   - Spotify OAuth/Web API access is allowed for search and playback behaviors that Windows media sessions cannot provide.
   - Example: "Play Ghost — Witch Image on Spotify."
   - Return a short spoken acknowledgment through David's local TTS server so the result is clear without reading the wrist.
   - Never leave a microphone open continuously.

7. **Mute-safe VRChat textbox dictation**
   - A separate microphone button on a Comms deck deliberately captures speech, previews the transcription, then sends approved text to VRChat's `/chatbox/input` OSC endpoint.
   - Example: David says “hi dips”; Interfayce posts `hi dips` in VRChat.
   - This must never enable, route into, or otherwise reveal the VRChat voice microphone.
   - Show armed/listening/transcribing/sent feedback and provide a cancel-before-send path.

8. **Spotify audio routing into VRChat**
   - Provide an Interfayce-owned virtual microphone route rather than depending on VAC or Voicemeeter.
   - Leave Spotify's normal local playback alone; capture only Spotify for the broadcast feed.
   - Require an explicit, obvious gate before music is fed to VRChat.
   - Be dormant/silent outside a deliberate VR music-broadcast session; avoid accidental DJ mode and feedback loops.

## Interaction principles

- **Muscle memory wins.** Do not replace a working physical action with a prettier menu.
- **Deliberate before destructive.** Playspace movement, reset, and audio broadcast need purposeful activation.
- **Direct manipulation beats smoothing.** Playspace drag should follow the hand, not fight it.
- **Status is glanceable; controls are reachable.** Persistent UI should be small and quiet.
- **Tools enter the world when summoned.** A utility-deck action spawns its overlay at a comfortable eye-line distance, floating and immediately grabbable/moveable—not trapped on the wrist.
- **Personal, not generic.** Hard-coded or opinionated shortcuts are acceptable when they serve David's actual setup.
- **Build the useful thing first.** Fancy integration comes after a reliable, testable core.

## Wrist cockpit information architecture

The inner-wrist cockpit is one compact panel with a persistent top control strip and one lower **deck**. Selecting a strip button swaps the lower deck; it does not spawn more wrist menus. World-space desktop windows remain a separate, deliberate action from the Desktop deck.

Visually, it is a clean, restrained cyberpunk/shipboard instrument: smoked translucent panel (not invisible), near-black blue base, precise pale text, and small cyan/purple state accents. Avoid big bloom, animated noise, or decorative HUD clutter that steals contrast from VRChat.

The cockpit should not constantly sit in David's view. It wakes when the inner wrist is turned toward the HMD/gaze, uses a small on/off hysteresis plus a short fade to prevent flicker, and otherwise hides/sleeps. The gaze/pose gate controls visibility only; it must not prevent the underlying playspace drag gesture from working.

```text
┌──────────────────────────────────────────────────────┐
│  Music     Desktop     Playspace     Rig/Batteries    │  ← persistent strip
├──────────────────────────────────────────────────────┤
│                                                      │
│             selected lower deck                      │
│                                                      │
└──────────────────────────────────────────────────────┘
```

### Music deck

- current artist/title at a glance;
- previous, play/pause, next;
- volume later if needed;
- deliberate microphone/voice-command arm control (not always listening);
- one-shot VRChat music-note announcement remains a separate opt-in state.

### Comms deck

- a microphone button dedicated to VRChat textbox dictation, visually distinct from the Music command microphone;
- local transcription preview with cancel and send controls;
- OSC chatbox output only—never VRChat voice activation or audio routing;
- the command and dictation microphones must never silently cross-route between modes.

### Desktop deck

- personal app launch/spawn shortcuts (weather, email, Discord, browser, etc.);
- spawn selected window at eye-line, immediately grabbable;
- spawn a VR keyboard only on demand for text input;
- later: per-window sleep/freeze/close state.

### Playspace deck

- current session offset / active-drag indication;
- a prominent hold-to-confirm **Restore session baseline** control;
- set-floor and set-floor-plus-center route when finished;
- eventually one-step undo for the last floor operation.

### Rig/Battery deck

- compact summary for the 8 logical SlimeVR body slots plus both Index controllers;
- expanded slot detail for connection/battery/runtime estimate;
- clear route to full SlimeVR recovery/body-mount calibration;
- no duplicate drift-reset control.

### Playspace escape hatch

The wrist utility deck must always expose **Restore session baseline**. It restores the exact SteamVR standing-origin transform Interfayce captured when it started; it must never infer a new center from the headset or alter SteamVR's saved room setup. Make it hold-to-confirm to avoid a stray reset, but keep it quick and obvious when tracking/playspace behavior gets cursed.

## Explicit non-goals for the first build

- Rebuilding all of OVR Advanced Settings.
- Rebuilding OVR Toolkit or copying its proprietary implementation.
- Replacing SlimeVR's tracker-side double-tap drift reset.
- A general-purpose, always-on desktop environment in VR.
- An app store-ready multi-user product.
