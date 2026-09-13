# Interfayce build status

## Installed candidate: 1.2.20

Built and installed September 13, 2026; overlay and resident service are running.
Codex subscription ASK integration adds streamed replies, conversation resumption,
scrolling, cancellation, decision cards and cancellable speech. Settings -> ASK
selects the Codex model; default is GPT-5.6 Luna, low reasoning, standard tier.
Music's API model is unchanged. See CODEX-USAGE.md for controls and limitations.

182 Python tests passed, one skipped. Native regressions and Release build passed;
live Codex streaming, recall after reconnect, interruption and Luna selection passed.
Installed binaries match staging and settings were preserved. The maintainer confirmed the integration works perfectly in VR.
Independent ASK and full-application reviews are complete; see APPLICATION-REVIEW.md.
ASK review fixes pass 188 Python tests and native build/layout checks in the working
tree. Those fixes are not installed yet; code division has not started.

Installer: packaging/out/installer/Interfayce-Setup-1.2.20.exe. SHA-256:
4311B61F6F822EFD1E7C4A13BE9DE4942FCA3CC2331568A3ABD712DA48C4C4BF

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
