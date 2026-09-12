# Optional MolotovCherry v0.3.1 backend; run elevated. No security-mode changes.
$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'out/molotov-setup.log'
Start-Transcript -LiteralPath $log -Force | Out-Null
try {
    $kit = Join-Path $PSScriptRoot '.cache/molotov-0.3.1/portable'
    $certificate = Join-Path $kit 'DriverCertificate.cer'
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificate)
    if ($cert.Thumbprint -ne '5DFA7E68997ED52D4B10420407860D1C29B6F427') { throw 'Unexpected driver certificate' }
    $devcon = Join-Path $PSScriptRoot '.cache/vdd-25.7.23/control/Dependencies/devcon.exe'
    if ((Get-AuthenticodeSignature -LiteralPath $devcon).Status -ne 'Valid') { throw 'DevCon signature invalid' }
    $catalog = Get-AuthenticodeSignature -LiteralPath (Join-Path $kit 'virtualdisplaydriver.cat')
    if ($catalog.SignerCertificate.Thumbprint -ne $cert.Thumbprint) { throw 'Catalog signer does not match certificate' }
    $existing = Get-PnpDevice -Class Display | Where-Object {
        (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_HardwareIds).Data -contains 'Root\VirtualDisplayDriver'
    }
    if ($existing) { throw 'Molotov driver already installed; not creating a duplicate' }
    if (Test-Path 'HKLM:/SOFTWARE/VirtualDisplayDriver') { throw 'Existing Molotov settings found; left untouched' }
    foreach ($store in @('Root','TrustedPublisher')) {
        if (-not (Test-Path "Cert:/LocalMachine/$store/$($cert.Thumbprint)")) {
            Import-Certificate -FilePath $certificate -CertStoreLocation "Cert:/LocalMachine/$store" | Out-Null
            Write-Output "Imported pinned driver certificate into $store"
        }
    }
    if ((Get-AuthenticodeSignature -LiteralPath (Join-Path $kit 'virtualdisplaydriver.cat')).Status -ne 'Valid') {
        throw 'Catalog verification failed after certificate import'
    }
    & reg.exe import (Join-Path $kit 'install.reg')
    if ($LASTEXITCODE -ne 0) { throw 'Registry initialization failed' }
    Set-ItemProperty 'HKLM:/SOFTWARE/VirtualDisplayDriver' -Name data -Value '[{"id":0,"modes":[{"width":1920,"height":1080,"refresh_rate":60}]}]'
    & $devcon install (Join-Path $kit 'VirtualDisplayDriver.inf') 'Root\VirtualDisplayDriver'
    if ($LASTEXITCODE -notin @(0,1)) { throw "Driver installation failed: $LASTEXITCODE" }
    Get-PnpDevice -Class Display | Where-Object FriendlyName -eq 'Virtual Display' | Format-List Status,FriendlyName,InstanceId
} finally {
    Stop-Transcript | Out-Null
}
