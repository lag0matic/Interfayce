# Interfayce Virtual Audio: controlled test installation

This file is a runbook, not an automatic installer. Building the package is
safe and offline. The certificate, boot-policy, and driver-install commands
below are intentionally manual because they require elevation and a restart.

## What the package creates

- `Speakers (Interfayce Virtual Audio)`: the private render endpoint owned by
  the user-mode audio engine.
- `Microphone Array (Interfayce Virtual Audio)`: the capture endpoint VRChat
  can select.
- Hardware ID: `ROOT\InterfayceVirtualAudio`.
- Kernel service: `InterfayceVirtualAudio`.

The microphone returns silence when the feed underruns or the user-mode engine
is stopped. Interfayce never changes the Windows default input or output.

## 1. Build only

From the repository root:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  .\native\driver\InterfayceVirtualAudio.sln `
  /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

Expected package:

```text
native\driver\x64\Debug\package\InterfayceVirtualAudio.inf
native\driver\x64\Debug\package\InterfayceVirtualAudio.sys
native\driver\x64\Debug\package\interfaycevirtualaudio.cat
native\driver\x64\Debug\package.cer
```

This step does not install or load anything.

## 2. Preflight before any test installation

Run these read-only checks from an elevated PowerShell window and record the
results:

```powershell
Confirm-SecureBootUEFI
manage-bde -status $env:SystemDrive
bcdedit /enum "{current}"
```

Before changing boot policy, make sure the BitLocker recovery key is available.
Windows normally blocks `TESTSIGNING ON` while Secure Boot policy is enforcing
signed production drivers. Do not disable Secure Boot or suspend BitLocker as
an incidental troubleshooting step; decide on that separately at install time.

## 3. Trust this build's test certificate

Only after reviewing the preflight, from an elevated PowerShell window:

```powershell
$certificate = Resolve-Path .\native\driver\x64\Debug\package.cer
Import-Certificate -FilePath $certificate -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath $certificate -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

The certificate is local development trust, not a production signature.

## 4. Enable Windows test-signing and restart

This is a separate, explicit machine-state change:

```powershell
bcdedit /set testsigning on
Restart-Computer
```

After restart, verify rather than assume:

```powershell
bcdedit /enum "{current}"
```

## 5. Install the root-enumerated device

From an elevated PowerShell window at the repository root:

```powershell
$devcon = "C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe"
& $devcon install `
  .\native\driver\x64\Debug\package\InterfayceVirtualAudio.inf `
  'ROOT\InterfayceVirtualAudio'
```

Do not choose either endpoint as a Windows default. Confirm that both endpoints
appear, then run the non-destructive user-mode checks:

```powershell
.\native\build\bin\InterfayceAudioEngine.exe --list-render-endpoints
.\native\build\bin\InterfayceAudioEngine.exe --broadcast-spotify 5
```

The second command sends Spotify-only PCM to the Interfayce feed for five
seconds. It does not select the microphone in VRChat or change Spotify's normal
headphone output.

## Full rollback

First stop `InterfayceAudioEngine.exe` and make sure VRChat is not using the
virtual microphone. From an elevated PowerShell window:

```powershell
$devcon = "C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe"
& $devcon remove 'ROOT\InterfayceVirtualAudio'
pnputil /enum-drivers /class Media
```

In the `pnputil` output, identify the published `oemNN.inf` whose original name
is `InterfayceVirtualAudio.inf`, then remove that exact package:

```powershell
pnputil /delete-driver oemNN.inf /uninstall /force
```

Remove the `InterfayceVirtualAudio` development certificate from both **Local
Computer** certificate stores (`Trusted Root Certification Authorities` and
`Trusted Publishers`) using `certlm.msc`, after checking its subject and
thumbprint. Finally disable test-signing and restart:

```powershell
bcdedit /set testsigning off
Restart-Computer
```

After restart, verify that the two endpoints and kernel service are absent and
that `testsigning` is off. Restore any Secure Boot or BitLocker state changed
during the approved test procedure.
