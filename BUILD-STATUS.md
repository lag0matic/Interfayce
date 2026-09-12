# Interfayce build status

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
