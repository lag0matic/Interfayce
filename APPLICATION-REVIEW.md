# Interfayce application review — September 13, 2026

The maintainer confirmed 1.2.20 works perfectly in VR. Three independent reviewers
first examined the ASK integration, then performed a broader pass over the native
application, Python services, configuration, packaging and recovery code. Findings
below consolidate duplicates. This is a source review with targeted offline tests,
not exhaustive end-to-end validation of every device or Windows application.

## Overall assessment

Interfayce has useful existing boundaries: desktop capture, private-window recovery,
battery estimation, process audio, and chatbox scheduling each have focused
components. Preserve those. The main maintenance problem is shared state and work
ownership spanning the native frame loop, Python VoiceRuntime and settings UI.
There is no evidence here that a wholesale rewrite would help.

No high-confidence critical defect was identified in private-window recovery,
battery tracking, or playspace restoration. That is a review result, not a guarantee.
All confirmed findings were rated P2: specific correctness or responsiveness defects
with narrower triggers than ordinary successful use.

## Broader findings, still open

| Priority order | Area | Trigger and consequence | Evidence | Recommended correction |
|---|---|---|---|---|
| 1 | Service polling | A service that accepts connections but delays replies can repeatedly freeze the native VR loop for up to 750 ms. Settings polling affects every deck; COMMS polls more frequently. | `native/src/main.cpp`: `ReadTtsSettings`, `RequestCommsState`, periodic polling blocks | Use one outstanding asynchronous request per feed and consume completed snapshots on the frame loop. |
| 2 | Desktop drag shutdown | Exiting during a display drag tears down surfaces without releasing the injected mouse button. Windows can retain a held-button state. | `native/src/main.cpp`: active pointer release paths and final `desktopSurfaces.Shutdown()` | Release owned primary/secondary pointer sessions before surface teardown, including a display-injection fallback. |
| 3 | Captured-window dragging | Posted mouse-move events have no held-button flag. Controls that inspect MK_LBUTTON can fail to select text or drag sliders. | `native/src/desktop_surface_registry.cpp`: `InjectWindowPointer`, lines 261–263 | Track pressed buttons and retain the drag's child target until release. |
| 4 | Captured-window scrolling | Wheel input uses global cursor injection. A desktop window covering those coordinates can receive the scroll instead of the captured window. | `native/src/desktop_surface_registry.cpp`: `SendScrollEvent`, lines 1216–1228 | Target window captures explicitly; keep global injection for display captures. |
| 5 | Settings updates | Overlapping changes read the same old settings and overwrite one another. Desktop/service processes also share a fixed temporary filename. | `src/interfayce/settings.py`: `save_settings`, `adjust_tts_volume` and other read/modify/write helpers | Give settings one writer or cross-process transactions, unique temporary files and field-specific mutations. |
| 6 | STT configuration | Applying a different remote endpoint/model, or removing the endpoint, leaves the running recognizer using the old route until restart. | `src/interfayce/voice_service.py`: runtime construction and shared transcriber references; `settings_window.py`: Apply | Reconfigure at an idle capture boundary and update Comms, ASK/Music and health together. |
| 7 | Live speech controls | Mute/volume changes do not affect speech already playing because playback reads settings once. | `src/interfayce/kokoro.py`: `play_wav` | Consult a shared live playback-control state between chunks. |
| 8 | Deterministic Music commands | Without the legacy LLM configured, pause/resume/stop all toggle playback. Repeating pause can resume music. | `src/interfayce/voice.py`: `parse_music_intent`, `execute_music_intent` | Separate explicit play and pause operations; reserve toggle for toggle requests. |

The reviewers reproduced lost settings increments, live mute behavior and Music
toggle behavior with in-memory fakes. The remaining broad findings were established
from call paths; no live desktop input or remote service experiments were performed.

## ASK findings corrected in the working tree

- Pass capture's cancellation event into the backend so Stop cannot be cleared
  between transcription and turn submission.
- Discard a known-dead idle connection and resume before submitting the next new
  prompt. Continue to avoid replaying uncertain submissions.
- Track connection initialization ownership and reject publication after close.
- Use shared rectangular bounds for drawn decision buttons and click targets.
- Start new decision cards at the top and preserve the previous chat scroll state.
- Dispatch decision controls asynchronously, with a bounded pending control and
  Stop priority, instead of blocking the frame loop.
- Correlate file approvals with actual change items. Offer Stop if a complete,
  suitably bounded change cannot be shown.
- Track message items and phases, keeping commentary separate from final-answer
  speech. Retain a fallback for providers without phase metadata.

Validation: 188 Python tests passed, one skipped; native Release build passed;
decision geometry regression checks passed; the production-renderer decision
preview was inspected. These review fixes are source changes, not yet installed.
The installed, user-validated release remains 1.2.20. No broad finding above has
been silently marked fixed.

## Proposed division order

Fix the concrete behavior defects in small changes first, then checkpoint before
structural movement. Each extraction should preserve the resulting behavior.

1. **Native service client and polling:** transport, deadlines, cached snapshots,
   background work and completion handling. Removes service waits from the VR loop.
2. **Desktop input router:** target identity, pressed buttons, drag ownership,
   scrolling and release on shutdown. One place to enforce input lifetime rules.
3. **Settings and speech controllers:** atomic configuration changes, microphone
   ownership, recognizer refresh and live output controls. UI and HTTP handlers
   become adapters rather than competing state owners.
4. **Wrist deck controllers and views:** separate per-deck actions/state from
   rendering, with shared hit-test geometry. ASK's item reducer is already isolated.
5. **VR session orchestration:** retain a smaller main loop for tracking, frame
   updates and ordered startup/shutdown. Keep the existing focused recovery,
   capture, battery and audio components intact.

Lower-priority follow-ups: runtime discovery/version diagnostics, model-list
pagination, and more transport-level failure fixtures. These should not displace
the demonstrated input, responsiveness and configuration defects.

## Follow-through: 1.2.21 source checkpoint

All eight broader findings have implementations in the working tree: asynchronous
periodic polling; surface-owned mouse release/drag state and targeted window wheel
messages; cross-process settings transactions with desktop baseline merging;
shared configurable STT routing; live playback controls; and explicit Music
play/pause. Python checks: 192 passed, one skipped. Native Release build passed.
The following phase extracts these boundaries without changing their behavior.
Installation and in-headset validation of this revision follow the structural pass.


## Structural checkpoint: 1.2.21

The first structural pass separates authenticated native service transport
(`local_service_client`), physical desktop pointer/wheel routing and held-button
ownership (`DesktopInputRouter`), cross-process settings persistence
(`SettingsStore`), and ASK rendering (`overlay_renderer_assistant`). Shared glyph
identifiers live in `holo_glyph.h`; decision geometry remains shared with input.
Configured STT routing and live playback controls were separated with the fixes.

Validation: 192 Python tests passed, one skipped. Native Release and panel preview
builds passed. Native regressions passed for click filtering, real mixed-DPI
coordinates, captured child drag ownership, teardown release, targeted wheel
messages, decision geometry, artwork, battery estimation and private-window
recovery. The ASK decision preview was visually checked after extraction.

This is a bounded first pass, not a wholesale rewrite: main.cpp still coordinates
VR state and completed service work; other wrist decks remain in the renderer.
Further division can follow these boundaries when those areas need changes.


Deployment: 1.2.21 installed successfully September 13, 2026. Installed binaries
match staging, settings were preserved, overlay/service are running, authenticated
health is ready and ASK reports Codex / READY. In-headset confirmation is pending.
Earlier source-only notes above describe the review sequence, not current deployment.
