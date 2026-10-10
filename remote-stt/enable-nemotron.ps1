[CmdletBinding()]
param([switch]$Rollback)
$ErrorActionPreference = 'Stop'
$kitRoot = $PSScriptRoot
if ($kitRoot.StartsWith('\\')) {
    $parts = $kitRoot.TrimStart('\').Split('\')
    if ($parts[0].Split('.')[0] -ine $env:COMPUTERNAME) {
        throw 'Run this helper on the STT server PC, not the VR PC.'
    }
    $share = Get-CimInstance Win32_Share | Where-Object Name -eq $parts[1]
    if (-not $share) { throw 'Cannot resolve the local server folder.' }
    $kitRoot = $share.Path
    if ($parts.Length -gt 2) { $kitRoot = Join-Path $kitRoot ($parts[2..($parts.Length-1)] -join '\') }
}
Set-Location -LiteralPath $kitRoot
$python = Join-Path $kitRoot '.venv\Scripts\python.exe'
$configPath = Join-Path $kitRoot 'config.json'
if (-not (Test-Path -LiteralPath $python)) { throw 'Existing STT environment not found.' }
New-Item -ItemType Directory -Path (Join-Path $kitRoot 'logs') -Force | Out-Null
Start-Transcript -Path (Join-Path $kitRoot 'logs\nemotron-upgrade.log') -Append
try {
    if (-not $Rollback) {
        $gpuTool = Get-Command nvidia-smi -ErrorAction SilentlyContinue
        if ($gpuTool) { & $gpuTool.Source --query-gpu=name,memory.total,memory.free --format=csv }
        # Add only the pinned runtime; do not upgrade Whisper/Moonshine or numpy.
        & $python -m pip install -r (Join-Path $kitRoot 'requirements-nemotron.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Nemotron runtime installation failed.' }
        & $python (Join-Path $kitRoot 'prepare-nemotron.py')
        if ($LASTEXITCODE -ne 0) { throw 'Model preparation failed.' }
        & $python (Join-Path $kitRoot 'benchmark-streaming.py') --require-realtime --output (Join-Path $kitRoot 'logs\nemotron-benchmark.json')
        if ($LASTEXITCODE -ne 0) { throw 'Model could not pass the live-streaming check; Moonshine is unchanged.' }
    }
    $original = [IO.File]::ReadAllText($configPath)
    $config = $original | ConvertFrom-Json
    if ($Rollback) {
        $selected = 'moonshine'
    } else {
        $selected = 'nemotron'
        $config.models | Add-Member -Force NoteProperty nemotron ([pscustomobject]@{
            backend='nemotron'; model_directory='nemotron-en-560ms'; provider='cpu'; num_threads=4
        })
    }
    $config | Add-Member -Force NoteProperty streaming_model $selected
    $backupRoot = Join-Path $kitRoot 'update-backups'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $backupRoot ('config-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '.json')), $original)
    $temporary = $configPath + '.pending'
    [IO.File]::WriteAllText($temporary, ($config | ConvertTo-Json -Depth 12))
    Move-Item -LiteralPath $temporary -Destination $configPath -Force
    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kitRoot 'restart-for-update.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Restart helper failed.' }
        $ready = $false
        for ($i=0; $i -lt 120; $i++) {
            Start-Sleep -Seconds 1
            try {
                $health = Invoke-RestMethod -Uri "http://127.0.0.1:$($config.port)/health" -Headers @{Authorization="Bearer $($config.api_key)"} -TimeoutSec 2
                if ($health.streaming_model -eq $selected -and $health.models.$selected.loaded) { $ready=$true; break }
                if ($health.models.$selected.error) { throw 'Selected model failed to load.' }
            } catch { }
        }
        if (-not $ready) { throw 'New streaming model did not become healthy.' }
        Write-Host "Live captions now use $selected. Whisper music commands are unchanged."
    } catch {
        [IO.File]::WriteAllText($configPath, $original)
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kitRoot 'restart-for-update.ps1')
        throw 'Activation failed. Previous configuration restored; inspect logs/nemotron-upgrade.log.'
    }
} finally { Stop-Transcript }
