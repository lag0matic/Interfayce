#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $env:TEMP 'Interfayce-driver-install.txt'
$lines = [System.Collections.Generic.List[string]]::new()

trap {
    $lines.Add("install_error=$($_.Exception.Message)")
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    exit 1
}

$bootEntry = (& bcdedit /enum '{current}' 2>&1 | Out-String)
if ($bootEntry -notmatch '(?im)^testsigning\s+Yes\s*$') {
    throw 'Windows is not currently booted with test-signing enabled.'
}
if (Confirm-SecureBootUEFI) {
    throw 'Secure Boot is enabled; refusing the development-driver install.'
}

$certificatePath = Join-Path $PSScriptRoot 'x64\Debug\package.cer'
if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    throw "Missing package certificate: $certificatePath"
}
$packageCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $certificatePath
)
$certificateThumbprint = $packageCertificate.Thumbprint
foreach ($store in @('Root', 'TrustedPublisher')) {
    $certificate = Get-ChildItem "Cert:\LocalMachine\$store\$certificateThumbprint" `
        -ErrorAction SilentlyContinue
    if ($null -eq $certificate) {
        throw "Interfayce test certificate is missing from LocalMachine\$store."
    }
}

$infPath = Join-Path $PSScriptRoot 'x64\Debug\package\InterfayceVirtualAudio.inf'
$devconPath = 'C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe'
if (-not (Test-Path -LiteralPath $infPath -PathType Leaf)) {
    throw "Missing driver INF: $infPath"
}
if (-not (Test-Path -LiteralPath $devconPath -PathType Leaf)) {
    throw "Missing DevCon: $devconPath"
}

$installOutput = (& $devconPath install $infPath 'ROOT\InterfayceVirtualAudio' 2>&1 | Out-String).Trim()
$lines.Add('devcon_begin')
$lines.Add($installOutput)
$lines.Add('devcon_end')
if ($LASTEXITCODE -ne 0) {
    throw "DevCon failed with exit code $LASTEXITCODE."
}

$lines.Add('install_completed=True')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
