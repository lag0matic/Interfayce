# Optional private display setup

Keep in VR needs a working extended virtual monitor. Interfayce does not silently
install one. The tested setup uses MolotovCherry virtual-display-rs v0.3.1 with
one 1920x1080/60 Hz display and NVIDIA 616.92.

These setup scripts are maintainer helpers, not standalone downloaders. They
expect prepared upstream packages in the ignored packaging cache or setup kit.
Review their prerequisites before running. Molotov setup validates a pinned
certificate and imports it into LocalMachine Root and TrustedPublisher with
administrator access. Secure Boot and test-signing settings are unchanged.

Confirm Windows uses Extend, then run InterfaycePrivateWindowTests.exe --displays:
one private candidate and a physical destination must be present. The --live
option parks a harmless test window and returns it minimized. Installation
success alone does not establish activation.

If Extend immediately reverts to Disconnect, check graphics-driver compatibility
before repeated reinstalls. Both tested virtual drivers failed with NVIDIA
616.64; Molotov activated after updating to 616.92.

Use Recover VR windows before disabling the virtual display. It remains connected
when Interfayce closes. See ../RECOVERY.md for recovery behavior.
