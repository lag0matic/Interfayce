# Interfayce build status

## Installed candidate: 1.2.23

Adds a persistent BLOCK GAME INPUT toggle in the DESK wrist footer, visible on
both the home and window-list views. Default Off. Turning it On enables SteamVR's
globalActionSetPriority prerequisite and selectively raises the Interfayce action
set priority for the hand aiming at a desktop surface, keyboard, frame, or DESK
wrist view. This covers sources bound to Interfayce, not every controller input.
Ownership lasts through held clicks, grabs and scrolling, including pointer drift;
tracking loss, unavailable support and app exit release the claim. Off uses the
original unrestricted input path once held interactions finish.

The preference is stored through SteamVR settings in com.lag0matic.interfayce /
desktopBlockGameInput. Turning it off leaves SteamVR's general overlay support
available to other apps. A disabled prerequisite or rejected priority update shows
UNAVAILABLE; rejected input updates fall back to ordinary controls. Windows mouse
injection is a separate path and is not blocked by SteamVR action priority.

Validation: native Release and production preview builds passed; ownership tests
cover Off, unavailable support, independent hands, dragging out, toggling off
while held, release and tracking loss. SteamVR accepted the per-hand action-set
probe (error 0). Footer previews checked. 193 Python tests passed, one skipped.
Installed September 15, 2026: overlay and service running, health 200 ready,
installed binaries match staging, settings preserved. Actual VRChat suppression
needs an in-headset test with the toggle enabled.
Installer SHA256: 5163ADBE75BEA4EDB9EE2272B08B2E586DD5F335BA772B5D3A1290DAE23FB193.

## Installed candidate: 1.2.22

Adds approximately 200 ms of microphone capture after releasing chatbox
push-to-talk, so releasing while finishing a word does not immediately end the
audio. The additional frames reach both streaming STT and the retained fallback
recording. The original maximum recording duration still applies. This is an
experiment for final-word errors, not a confirmed fix for recognition accuracy.

193 Python tests passed, one skipped. The new capture test verifies tail delivery
and the hard duration limit. Native Release build passed. Installed September 14, 2026; overlay and service
are running, authenticated health is ready, installed binaries match staging,
and settings were preserved. The 1.2.21 checkpoint remains the last VR-validated
build. Awaiting practical confirmation of the release buffer.

Installer SHA256: 827095F2C6967A55E38CE6D665B0E023702061C11C7C6C82A96B591C6E91034A.

## Validated checkpoint: 1.2.21

Built and installed September 13, 2026. Overlay and resident service are running;
authenticated health returns 200 ready and ASK reports Codex / READY. Installed
native, service and recovery binaries match staging. Settings were preserved.
Installer completed successfully without requiring a Windows restart.

Includes the ASK and full-application review fixes, then a separate structural
pass for native service transport, desktop input ownership, settings persistence
and ASK rendering. See APPLICATION-REVIEW.md for findings and scope. Codex ASK
retains the economical Luna default and Settings -> ASK model selection.

Validation: 192 Python tests passed, one skipped. Release and production-preview
builds passed. Native checks passed for input ownership/release/scroll targeting,
click filtering, real mixed-DPI coordinates, decision geometry, artwork, battery
estimation and private-window recovery. ASK preview visually checked. The maintainer confirmed a successful VR session with 1.2.21 and approved this
checkpoint for GitHub. Occasional plausible STT word substitutions remain an
observation; diagnostic recording is deferred. No new model turn was needed
for the fix/refactor validation.

Local checkpoints: d7efea8 (feature and review fixes), c26e109 (structural pass).
Installer: packaging/out/installer/Interfayce-Setup-1.2.21.exe. SHA-256:
88482CAF9101C5F39283FC525F7C171DB6AEC103A6C9118FC58B5F0404EF3EA4

## Previous checkpoint: 1.2.19

Built and installed September 12, 2026. Wrist scrolling and captured-app input on
a 125% secondary monitor are confirmed working in VR by the maintainer.

Recent changes: persistent battery/service/clock strip with hover explanations;
scrollable wrist window list; mixed-DPI app input and click-jitter protection;
private VR windows with independent recovery; streaming STT and local fallback;
capture cues; queued song announcements; flexible Spotify requests; artwork fixes.

171 Python tests pass. Native tests cover desktop clicks, actual monitor coordinate
mapping, private-window recovery, artwork and battery estimates. Production UI
previews cover seven panels and a scrolled seven-window list.

Installed binaries matched the stage; settings and protected credentials were
unchanged. Installer: packaging/out/installer/Interfayce-Setup-1.2.19.exe
Size: 523029575 bytes. SHA-256:
3C87B4D092972B63F44E26FD1C6334FC83584368402A5DF1068CE10750781B61

Build output, model payloads, local configuration and driver packages are excluded
from Git. See RECOVERY.md for rebuild and recovery instructions.

Service lights report availability, not speaker audibility or guaranteed request
success. Spotify status reflects its media session, not OAuth. Private windows
require a working extended virtual display; recover windows before disabling it.
MolotovCherry v0.3.1 was validated with NVIDIA 616.92. Windows/app popup behavior
prevents promising absolute privacy.

Build runtime is Python 3.12; audioop requires attention before Python 3.13+.
Native orchestration remains concentrated in main.cpp and should be split gradually
as features change. In-headset testing remains essential.
