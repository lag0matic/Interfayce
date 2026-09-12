[CmdletBinding()]
param()
$ErrorActionPreference = "Stop"
$kitRoot = $PSScriptRoot
# A shortcut through the share still needs to run on the server computer.
if ($kitRoot.StartsWith('\\')) {
    $parts = $kitRoot.TrimStart('\').Split('\')
    if ($parts[0].Split('.')[0] -ine $env:COMPUTERNAME) {
        throw "Run this helper on the server PC, not the VR PC."
    }
    $share = Get-CimInstance Win32_Share | Where-Object Name -eq $parts[1]
    if (-not $share) { throw "Could not resolve this server share to its local folder." }
    $kitRoot = $share.Path
    if ($parts.Length -gt 2) { $kitRoot = Join-Path $kitRoot ($parts[2..($parts.Length-1)] -join '\') }
}
$config = Get-Content -LiteralPath (Join-Path $kitRoot "config.json") -Raw | ConvertFrom-Json
$endpoint = "http://127.0.0.1:$($config.port)"
$headers = @{ Authorization = "Bearer $($config.api_key)" }
$python = Join-Path $kitRoot ".venv\Scripts\python.exe"
& $python -c "from moonshine_voice import Transcriber; assert hasattr(Transcriber, 'create_stream')"
if ($LASTEXITCODE -ne 0) { throw "Moonshine streaming runtime is missing; run install.ps1 -Backend both." }
try {
    $null = Invoke-RestMethod -Uri "$endpoint/admin/shutdown" -Method Post -Headers $headers -TimeoutSec 5
    Start-Sleep -Seconds 2
} catch {
    Write-Host "The old server did not accept self-shutdown; checking its listening process."
}
$listeners = @(Get-NetTCPConnection -LocalPort ([int]$config.port) -State Listen -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique)
foreach ($taskOwner in $listeners) {
    $taskProcess = Get-CimInstance Win32_Process -Filter "ProcessId = $taskOwner"
    $expectedPython = [IO.Path]::GetFullPath($python)
    $expectedScript = Join-Path $kitRoot "server.py"
    $isPython = $taskProcess.ExecutablePath -and
        ([IO.Path]::GetFileName($taskProcess.ExecutablePath) -in @("python.exe", "pythonw.exe"))
    $isThisKit = $taskProcess.CommandLine -and (
        [string]::Equals($taskProcess.ExecutablePath, $expectedPython, [StringComparison]::OrdinalIgnoreCase) -or
        $taskProcess.CommandLine.IndexOf($expectedScript, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    if (-not $isPython -or -not $isThisKit -or $taskProcess.CommandLine -notmatch 'server\.py') {
        throw "Port $($config.port) is owned by a different or inaccessible process. Close the old STT server manually, then run this helper again."
    }
    Write-Host "Stopping verified Interfayce STT process $taskOwner."
    Stop-Process -Id $taskOwner -ErrorAction Stop
    Wait-Process -Id $taskOwner -Timeout 10 -ErrorAction SilentlyContinue
}
& (Join-Path $kitRoot "start.ps1")
if ($LASTEXITCODE -ne 0) { throw "Server restart failed." }
Write-Host "Server restarted. Both speech models will warm in the background."
