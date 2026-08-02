# Interfayce — Build Status

Updated: 2026-08-02

## Desktop surfaces: active branch checkpoint

Branch: `codex/desktop-surfaces`

The current offline-verified slice now:

- filters the display/application inventory to visible user-facing applications, excluding Windows shell hosts such as `explorer.exe`;
- gives the wrist Desktop deck icon-first controls for New Surface, open-surface management, and keyboard;
- spawns an independent world-space picker overlay at the current HMD eye line;
- renders display and application choices in that spawned surface using the shared wrist-panel D3D11 device;
- tracks each spawned surface independently and supports Bring to me and Close from the wrist list;
- destroys only the Interfayce VR surface on Close, never the underlying application;
- includes a non-VR `--desktop-sources` probe for checking the real filtered inventory.

The native sources and a separately named verification executable compile and link successfully while the prior live host remains open. Live VR verification is still pending. Picker pointer input, source assignment/capture, application input forwarding, grip movement, and two-hand scaling are not implemented yet.

Confirmed interaction direction:

- source selection lives inside each newly spawned world surface, not on the wrist;
- trigger ray interacts with captured content;
- one-hand grip on the frame moves a surface;
- gripping with both hands stretches or compacts a surface;
- the wrist list is the recovery route for lost surfaces.

## Handoff: current state (desktop work is incomplete)

The native host builds and launches from `native/build/bin/InterfayceOverlay.exe`. Live-tested: session-safe playspace drag/reset, inner-wrist fit, in-world trigger ray interaction, Spotify controls/album art, VRChat OSC announce + clear, Index/Slime batteries, and guarded SlimeVR Full Reset + Body Mount.

The desktop-window request is **not complete**.

What exists:

- `native/src/desktop_surface_manager.h/.cpp` read-only enumerates displays (`EnumDisplayMonitors`) and eligible titled top-level app windows (`EnumWindows`). `EnumerateSources()` combines them.
- The Desktop deck shows a live display/window count when selected.
- `KEYBOARD` opens SteamVR's native keyboard via `ShowKeyboardForOverlay`.
- `WINDOWS / NEXT` is only a visual placeholder; no source selection, capture, spawned overlay, movement, list, or Bring-to-me feature exists yet.

Next implementation sequence:

1. Render paged/selectable `DesktopSource` cards in Desktop (remove the temporary use of `musicLine` as its inventory title).
2. Add `DesktopSurfaceRegistry`: independent OpenVR overlay, stable source ID, transform, visibility, and close state for every chosen source.
3. Use Windows Graphics Capture (C++/WinRT `Windows.Graphics.Capture`) for display and `HWND` capture; feed frames into compositor-GPU D3D11 textures. Do not use GDI/DWM thumbnails for app capture.
4. Spawn each capture at HMD eye line, independent and immediately grabbable. Use a separate safe grab action; never reuse B-double playspace drag.
5. Add the open-surface list: Bring to me, hide/show, close. Bring-to-me reposes one surface without recreating capture or disturbing others.
6. Add pointer/input routing only after capture/spawn works; view-only is acceptable initially because the native keyboard already exists.

Constraints to preserve:

- `OverlayRenderer` now retains one D3D11/DXGI shared texture and repaints it in place. Do not regress to recreating GPU devices/textures for panel updates.
- Keep `InnerLeftWristTransform()` in `native/src/main.cpp` unchanged.
- Right trigger is UI click. Index B double-tap/hold is playspace drag only.
- Stop `InterfayceOverlay` before a rebuild if the linker locks it.
- SlimeVR Node adapters are development-only and currently reference `C:\Users\lag0m\AppData\Local\Temp\slimevr-server-reference`; vendor/pin before packaging.

Last verification:

- Native Release build succeeded after adding `DesktopSurfaceManager`.
- Python suite: 27 passing before the last native-only desktop inventory changes.

## North-star decisions

- This is David/Lag0Matic's personal, low-overhead VRChat cockpit—not a general consumer product.
- Windows + SteamVR/OpenVR first; Linux/OpenXR later.
- Native C++20 is the target host architecture: OpenVR initially, D3D11 render-to-texture for the persistent wrist overlay, no Electron/browser/UI framework runtime.
- Wrist UI mounts on the **inner wrist**, palm-up, with a quiet status layer and a multi-purpose utility deck.
- Personal 10-trackers setup is represented as 8 logical SlimeVR body slots (elbows, chest, hip, thighs, combined ankle+foot) plus independent Index controller slots.

## Confirmed working

- Spotify media-session discovery and transport controls work through Windows media APIs.
- A stable Spotify change sends one silent VRChat OSC message: `♫ Artist — Title`.
- An experimental empty chatbox OSC payload clears that message after seven seconds; live-tested successfully in David's current VRChat build.
- OpenVR reads Index controller battery state (both controllers live-tested at 99%).
- OpenVR reads Index controller poses.
- OpenVR reads SteamVR's working standing-origin matrix once tracking is awake. This baseline is read-only so far.
- Floor-reference measurement works read-only: 25-sample stability test, lower controller selected.
  - David's canonical placement: Index controller **controls-up** on floor.
  - Observed tracked-origin offset: about 3.7 cm below current floor plane; back-down about 4.5 cm.
  - Treat as personal reference, not a universal magic constant.

## Playspace drag: intended behavior

- Preserve David's OVR Advanced Settings muscle memory: Index **B double-tap, hold second press, drag, release**.
- Lone press must never move the playspace.
- Current pure safety state machine: `src/interfayce/gesture.py`.
- Current pure transform math: `src/interfayce/transforms.py`.
- No code has written a SteamVR room transform yet.
- First actual write must be session-baseline backed, previewable, bounded, and have undo/restore-session-baseline.

## SteamVR Input research status

- Interfayce app/action manifest and an Index default binding exist under `assets/steamvr/`.
- SteamVR accepts and displays the binding, but a Python console process cannot keep the action source live as a proper overlay app.
- The legacy OpenVR `getControllerState` path does not expose the Index B press in this setup, even during a long hold.
- Conclusion: do not depend on either a Python host or legacy controller state for critical drag. Build the native overlay host, then revisit OpenVR Input actions inside that actual executable.

## Next build target

Build the smallest native C++20 SteamVR overlay host:

1. initialize as `VRApplication_Overlay`;
2. identify as `com.lag0matic.interfayce`;
3. load Interfayce's action manifest;
4. keep a low-cost event/action loop alive;
5. verify B double-tap action delivery **without** applying any playspace transform;
6. add the D3D11 overlay texture and inner-wrist pose after input is verified.

Do not start actual playspace writes until step 5 is proven.

The initial host source now lives in `native/`; it successfully compiles with the local Visual Studio Build Tools and completed a live SteamVR native probe (initialization, application identification, action-manifest loading, and D3D11 texture creation). It creates a single 768×384 smoked-glass texture and only uploads it once; the eventual UI must redraw it only when state is dirty.

Native Index action delivery is now proven live: both left/right actions reported active controller origins and David's right-hand B double gesture produced a clean active/release event. An initial `VRActiveActionSet_t` bug restricted the set to device index 0; setting `ulRestrictedToDevice` to `k_ulInvalidInputValueHandle` fixed it. Next: a dry-run controller-pose delta while held, with no SteamVR origin writes.

Native dry-run drag is also proven live: while David held the right-hand B-double gesture, Interfayce accumulated smooth per-frame controller translation and reported a final proposed offset of roughly `(-0.177 m, -0.064 m, -0.032 m)` on release. No SteamVR origin transform was written. Next is the deliberately guarded first working-origin preview/restore test.

The first native working-origin preview/restore test is now proven live. With the SteamVR dashboard closed (it captures controller focus while open), David used the familiar B-double held gesture; the playspace followed the controller during the hold and restored the exact captured baseline on release. The test never called `CommitWorkingCopy`. The special temporary-test launch argument has been removed from the normal native manifest.

Native session drag is now proven live: release keeps the moved playspace position for the Interfayce session, while the host's exit path restores the immutable startup baseline. This is the intended default behavior. It still never calls `CommitWorkingCopy`, never derives an offset from HMD position, and has a 2 m hard safety bound. The special session-test launch argument has been removed from the normal native manifest.

The normal native host now enables session drag by default. Diagnostic modes (`--probe`, `--input-capture`, `--drag-preview`, `--temporary-drag`, and `--show-bindings`) remain non-default and do not silently turn on session drag.

## Deferred but decided: first-party music broadcast

- Build an Interfayce-owned Windows virtual microphone later; do not depend on VAC/Voicemeeter for the final route.
- Spotify should continue to local headphones. Capture only Spotify with process-specific WASAPI loopback, then inject it into the virtual microphone only while a deliberate broadcast gate is on.
- The driver must be installed once and stay dormant/silent when VR/broadcast is off. Do **not** dynamically create/remove the endpoint during a VRChat session—the selected VRChat input would be invalidated.
- This requires a kernel-mode audio driver (SysVAD-style) and signing/test-mode work. Do not install a driver or change boot security without explicit approval and a recovery plan.

## Current test status

- Python proof-of-concept test suite: 27 passing checks.
- The native host has not been created yet; a prior attempted creation was aborted before any files were added after David paused to choose the backend.

## Native wrist panel: live fit baseline

- A native Direct2D/DirectWrite panel now renders successfully through an OpenVR `TextureType_DXGISharedHandle` texture. The raw-image test card confirmed that the overlay lifecycle and headset visibility path are sound; the shared DXGI handle fixed the previously invisible D3D texture handoff.
- David live-fitted the left inner-wrist panel using an avatar in VR. Current baseline is readable across the wrist, centered, clear of the arm, and should be retained as the starting fit:
  - panel width: `0.205 m`
  - controller-relative translation: `x=0.005 m`, `y=0.071 m`, `z=0.189 m`
  - controller-relative orientation: local panel normal along controller `+X`, with the panel rolled for across-wrist reading (`45°` clockwise from the prior test orientation)
- The current white/black diagnostic skin is being returned to the intended smoked-glass dark blue palette with sparse cyan accent.
- Next UI work: make the existing top strip (Music, Desktop, Playspace, Rig) interactive, then add a Wrist Fit deck with persisted size, local X/Y/Z nudge, and roll controls so David can make later fit changes without a rebuild.
- Interaction baseline now works in-world: the right Index trigger uses SteamVR's render-model `tip` component as its ray origin/direction, a small cyan dot appears at the actual panel intersection, brightens over an actionable control, and a 0.75-second trigger hold activates Playspace restore. It does not require the SteamVR dashboard.
- Appearance must be user-configurable later: glass, strip, primary text, muted text, accent, button, normal cursor, and actionable cursor colors belong in the local persisted Appearance/Wrist Fit settings rather than remaining fixed implementation values.
- Voice is now core scope: local Parakeet/Sherpa transcription feeds either a small LLM command router (Spotify/Interfayce actions) or mute-safe VRChat textbox dictation. Textbox mode sends only chatbox OSC and must never activate or route audio into VRChat voice.
- SlimeVR's local SolarXR WebSocket data feed is now live-proven read-only against David's running server: all 10 physical trackers report battery percentage, body assignment, and status. The Rig deck maps those into the agreed eight logical slots (left/right elbow, chest, hip, left/right thigh, left/right foot) and preserves independent Index controller batteries in its title row. The development adapter is `tools/slimevr_probe.cjs`; it uses the SlimeVR GUI's generated FlatBuffers protocol from the current local reference checkout. Before packaging Interfayce, vendor/pin that protocol adapter rather than relying on the temporary reference location.
- Native panel updates now repaint one persistent D3D11/DXGI shared texture; changing decks or receiving music/Rig state must not recreate the graphics device or overlay texture. While the Rig deck is selected, controller and Slime battery status refreshes every five seconds.
- The Rig recovery controls are live-tested: a one-second trigger hold on Full Reset and then Body Mount issued SlimeVR's expected audible reset/countdown sequence. Mounting remains disabled in the panel until SlimeVR's live server guard permits it. No drift-reset control is exposed.
- Desktop-surface foundation started: `DesktopSurfaceManager` inventories real Windows displays and eligible top-level application windows. Capture, OpenVR overlay ownership, and placement will remain separate so several independently movable desktop surfaces can exist and any one can be reposed in front of the HMD.
