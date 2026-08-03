#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $env:TEMP 'Interfayce-driver-enable-test-mode.txt'
$lines = [System.Collections.Generic.List[string]]::new()

trap {
    $lines.Add("enable_error=$($_.Exception.Message)")
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    exit 1
}

if (Confirm-SecureBootUEFI) {
    throw 'Secure Boot is enabled; refusing to change test-signing state.'
}

$bitLocker = (& manage-bde -status $env:SystemDrive 2>&1 | Out-String)
if ($bitLocker -notmatch 'Protection Status:\s+Protection Off') {
    throw 'BitLocker protection is not confirmed off; refusing to change boot state.'
}

$certificatePath = Join-Path $PSScriptRoot 'x64\Debug\package.cer'
if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    throw "Missing package certificate: $certificatePath"
}

$rootCertificate = Import-Certificate `
    -FilePath $certificatePath `
    -CertStoreLocation Cert:\LocalMachine\Root
$publisherCertificate = Import-Certificate `
    -FilePath $certificatePath `
    -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
$lines.Add("root_thumbprint=$($rootCertificate.Thumbprint)")
$lines.Add("publisher_thumbprint=$($publisherCertificate.Thumbprint)")

$bootOutput = (& bcdedit /set testsigning on 2>&1 | Out-String).Trim()
$lines.Add("bcdedit=$bootOutput")
if ($LASTEXITCODE -ne 0) {
    throw "BCDEdit failed with exit code $LASTEXITCODE."
}

$lines.Add('restart_required=True')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
