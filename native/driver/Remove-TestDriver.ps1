#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $env:TEMP 'Interfayce-driver-remove.txt'
$lines = [System.Collections.Generic.List[string]]::new()

trap {
    $lines.Add("remove_error=$($_.Exception.Message)")
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    exit 1
}

$devconPath = 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe'
if (-not (Test-Path -LiteralPath $devconPath -PathType Leaf)) {
    throw "Missing DevCon: $devconPath"
}

$removeOutput = (& $devconPath remove 'ROOT\InterfayceVirtualAudio' 2>&1 | Out-String).Trim()
$lines.Add('devcon_begin')
$lines.Add($removeOutput)
$lines.Add('devcon_end')
$deviceRemovalAccepted = $LASTEXITCODE -eq 0 -or
    $removeOutput -match '(?i)Removed on reboot|ready to be removed|No matching devices found'
if (-not $deviceRemovalAccepted) {
    throw "DevCon removal failed with exit code $LASTEXITCODE."
}

$driverInventory = (& pnputil /enum-drivers 2>&1 | Out-String)
$matchingBlock = ($driverInventory -split '(?:\r?\n){2,}') |
    Where-Object { $_ -match '(?im)^Original Name\s*:\s*InterfayceVirtualAudio\.inf\s*$' } |
    Select-Object -First 1
if ($null -ne $matchingBlock) {
    $publishedMatch = [regex]::Match(
        $matchingBlock,
        '(?im)^Published Name\s*:\s*(oem\d+\.inf)\s*$'
    )
    if (-not $publishedMatch.Success) {
        throw 'Found the Interfayce driver package but could not identify its published INF name.'
    }
    $publishedName = $publishedMatch.Groups[1].Value
    $deleteOutput = (& pnputil /delete-driver $publishedName /uninstall /force 2>&1 | Out-String).Trim()
    $lines.Add("published_inf=$publishedName")
    $lines.Add("pnputil=$deleteOutput")
    if ($LASTEXITCODE -ne 0) {
        throw "PnPUtil package removal failed with exit code $LASTEXITCODE."
    }
} else {
    $lines.Add('driver_store_package=not_present')
}

$certificatePath = Join-Path $PSScriptRoot 'x64\Debug\package.cer'
if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    throw "Missing package certificate used to identify trust entries: $certificatePath"
}
$packageCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $certificatePath
)
foreach ($store in @('Root', 'TrustedPublisher')) {
    $certificate = Get-Item "Cert:\LocalMachine\$store\$($packageCertificate.Thumbprint)" `
        -ErrorAction SilentlyContinue
    if ($null -ne $certificate) {
        Remove-Item -LiteralPath $certificate.PSPath -Force
        $lines.Add("certificate_removed=LocalMachine\$store\$($packageCertificate.Thumbprint)")
    } else {
        $lines.Add("certificate_absent=LocalMachine\$store\$($packageCertificate.Thumbprint)")
    }
}

$bootEntry = (& bcdedit /enum '{current}' 2>&1 | Out-String)
if ($bootEntry -match '(?im)^testsigning\s+Yes\s*$') {
    throw 'Interfayce was removed, but TESTSIGNING is still enabled in the boot entry.'
}
$lines.Add('testsigning_next_boot=Off')
$lines.Add('restart_required=True')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
