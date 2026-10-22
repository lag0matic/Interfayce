[CmdletBinding()]
param([switch]$Rollback)
$ErrorActionPreference = 'Stop'
$kitRoot = $PSScriptRoot
if ($kitRoot.StartsWith('\\')) {
    $parts = $kitRoot.TrimStart('\').Split('\')
    if ($parts[0].Split('.')[0] -ine $env:COMPUTERNAME) { throw 'Run this helper on the STT server PC.' }
    $share = Get-CimInstance Win32_Share | Where-Object Name -eq $parts[1]
    if (-not $share) { throw 'Cannot resolve server folder.' }
    $kitRoot = $share.Path
    if ($parts.Length -gt 2) { $kitRoot = Join-Path $kitRoot ($parts[2..($parts.Length-1)] -join '\') }
}
Set-Location -LiteralPath $kitRoot
$python = Join-Path $kitRoot '.venv\Scripts\python.exe'
$configPath = Join-Path $kitRoot 'config.json'
$original = [IO.File]::ReadAllText($configPath)
$config = $original | ConvertFrom-Json
$env:PATH = (Join-Path $kitRoot 'cuda-runtime') + ';' + $env:PATH
New-Item -ItemType Directory -Path (Join-Path $kitRoot 'logs') -Force | Out-Null
Start-Transcript -Path (Join-Path $kitRoot 'logs\whisper-streaming-upgrade.log') -Append
try {
    $selected = if ($Rollback) { 'nemotron' } else { 'whisper-turbo' }
    if (-not $Rollback) {
        Write-Host 'Checking buffered Whisper over a full 30-second utterance...'
        & $python (Join-Path $kitRoot 'benchmark-streaming.py') --config $configPath --repeat-seconds 30 --require-realtime --output (Join-Path $kitRoot 'logs\whisper-streaming-benchmark.json')
        if ($LASTEXITCODE -ne 0) { throw 'Whisper did not pass the streaming speed check. Current selection unchanged.' }
    }
    $backupRoot = Join-Path $kitRoot 'update-backups'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $backupRoot ('config-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '.json')), $original)
    $config | Add-Member -Force NoteProperty streaming_model $selected
    [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json -Depth 12))
    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kitRoot 'restart-for-update.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Restart failed.' }
        $ready = $false
        for ($i=0; $i -lt 120; $i++) {
            Start-Sleep -Seconds 1
            try {
                $health = Invoke-RestMethod -Uri "http://127.0.0.1:$($config.port)/health" -Headers @{Authorization="Bearer $($config.api_key)"} -TimeoutSec 2
                if ($health.streaming_model -eq $selected -and $health.models.$selected.loaded) { $ready=$true; break }
            } catch { }
        }
        if (-not $ready) { throw 'Selected model did not become ready.' }
        Write-Host "Live captions now use $selected."
    } catch {
        [IO.File]::WriteAllText($configPath, $original)
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $kitRoot 'restart-for-update.ps1')
        throw 'Activation failed; previous configuration restored. Check logs/whisper-streaming-upgrade.log.'
    }
} finally { Stop-Transcript }
