# Interfayce build status

## Installed candidate: 1.2.21

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
estimation and private-window recovery. ASK preview visually checked. The prior
1.2.20 integration was confirmed in VR by the maintainer; this revision still
needs their in-headset confirmation. No new model turn was needed for this pass.

Local checkpoints: d7efea8 (feature and review fixes), c26e109 (structural pass).
Installer: packaging/out/installer/Interfayce-Setup-1.2.21.exe. SHA-256:
88482CAF9101C5F39283FC525F7C171DB6AEC103A6C9118FC58B5F0404EF3EA4

## Current checkpoint: 1.2.19

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
