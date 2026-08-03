# Interfayce Virtual Audio

This directory contains the offline-buildable Windows virtual-audio driver for
Interfayce. It is derived from Microsoft's Simple Audio Sample in the official
Windows driver samples repository. The retained Microsoft sample license is in
`MICROSOFT-SAMPLE-LICENSE.txt`; Interfayce-specific bridge and identity changes
are identified in source comments and project history.

The driver exposes two fixed-format WaveRT endpoints. Windows currently
publishes their MMDevice UI names as `Speakers (Interfayce Virtual Audio)` and
`Microphone Array (Interfayce Virtual Audio)`:

- **Interfayce Broadcast Feed** — 48 kHz, stereo, signed 16-bit render endpoint
  written only by `InterfayceAudioEngine.exe`.
- **Interfayce Broadcast Microphone** — matching capture endpoint selected in
  VRChat.

Consumed feed frames enter a bounded half-second kernel ring. The microphone
drains that ring and fills any underrun with silence. Stopping or crashing the
user-mode engine therefore cannot leave stale audio broadcasting indefinitely.

## Offline build

Use the 64-bit MSBuild host. The WDK 28000 package omits the x86 INF verifier,
so the 32-bit MSBuild host cannot complete package validation.

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  .\native\driver\InterfayceVirtualAudio.sln `
  /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

Building does not install or load the driver. Enabling test-signing, trusting a
development certificate, changing Secure Boot/BitLocker state, and installing
the package are deliberately separate operations requiring explicit approval.
The manual preflight, test-install, verification, and full rollback sequence is
documented in `INSTALL-TESTING.md`.
