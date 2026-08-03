#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $env:TEMP 'Interfayce-driver-preflight.txt'
$lines = [System.Collections.Generic.List[string]]::new()

trap {
    $lines.Add("preflight_error=$($_.Exception.Message)")
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    exit 1
}

function Add-ReportLine([string]$Text) {
    $lines.Add($Text)
}

Add-ReportLine "timestamp=$([DateTime]::Now.ToString('o'))"
Add-ReportLine "administrator=True"

try {
    Add-ReportLine "secure_boot=$(Confirm-SecureBootUEFI)"
} catch {
    Add-ReportLine "secure_boot=unsupported:$($_.Exception.Message)"
}

$bitLockerOutput = (& manage-bde -status $env:SystemDrive 2>&1 | Out-String).Trim()
Add-ReportLine 'bitlocker_begin'
Add-ReportLine $bitLockerOutput
Add-ReportLine 'bitlocker_end'

$bootOutput = (& bcdedit /enum '{current}' 2>&1 | Out-String).Trim()
Add-ReportLine 'boot_entry_begin'
Add-ReportLine $bootOutput
Add-ReportLine 'boot_entry_end'

$package = Resolve-Path (Join-Path $PSScriptRoot 'x64\Debug\package')
foreach ($name in @(
    'InterfayceVirtualAudio.inf',
    'InterfayceVirtualAudio.sys',
    'interfaycevirtualaudio.cat'
)) {
    $path = Join-Path $package $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing package file: $path"
    }
    Add-ReportLine "package_file=$path"
}
$certificate = Join-Path $PSScriptRoot 'x64\Debug\package.cer'
if (-not (Test-Path -LiteralPath $certificate -PathType Leaf)) {
    throw "Missing package certificate: $certificate"
}
Add-ReportLine "package_certificate=$certificate"

$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
