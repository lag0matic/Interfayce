# One-time optional display setup. Run elevated only after the app build is ready.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Windows administrator approval is required. Run this setup as administrator.'
}
$kit = Join-Path $PSScriptRoot 'private-display-setup'
$driver = Join-Path $kit 'VirtualDisplayDriver'
$devcon = Join-Path $kit 'devcon.exe'
$catalog = Join-Path $driver 'mttvdd.cat'
foreach ($signedFile in @($devcon, $catalog)) {
    if ((Get-AuthenticodeSignature -LiteralPath $signedFile).Status -ne 'Valid') {
        throw "Signature verification failed: $signedFile"
    }
}
$expected = Get-Content -LiteralPath (Join-Path $kit 'hashes.json') -Raw | ConvertFrom-Json
foreach ($entry in $expected) {
    $file = Join-Path $kit $entry.path
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Package hash mismatch: $file"
    }
}
$existing = Get-PnpDevice -Class Display | Where-Object {
    $_.FriendlyName -eq 'Virtual Display Driver' -or $_.InstanceId -like 'ROOT\MTTVDD\*'
}
if ($existing) { throw 'A virtual display driver is already installed. Inspect its configuration instead of adding another.' }
$configurationRoot = Join-Path $env:SystemDrive 'VirtualDisplayDriver'
$configurationPath = Join-Path $configurationRoot 'vdd_settings.xml'
if (Test-Path -LiteralPath $configurationPath) {
    throw 'An existing virtual-display configuration was found and has been left untouched.'
}
New-Item -ItemType Directory -Force -Path $configurationRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $kit 'vdd_settings.xml') -Destination $configurationPath
$log = Join-Path $PSScriptRoot 'private-display-setup.log'
& $devcon install (Join-Path $driver 'MttVDD.inf') 'Root\MttVDD' *> $log
$code = $LASTEXITCODE
if ($code -notin @(0, 1)) {
    throw "Driver setup failed (exit $code). See $log. The driver/configuration are retained for diagnosis."
}
Write-Output 'Driver installed. Verify Windows uses Extend, keeps your physical primary display, and exposes one 1920x1080 virtual monitor before parking windows.'
if ($code -eq 1) { Write-Output 'Windows reports that a restart is required.' }
