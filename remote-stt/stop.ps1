$ErrorActionPreference = "Stop"
$pidFile = Join-Path $PSScriptRoot "server.pid"
if (-not (Test-Path -LiteralPath $pidFile)) {
    Write-Host "No server.pid was found."
    exit 0
}
$serverPid = [int](Get-Content -LiteralPath $pidFile -Raw)
$process = Get-Process -Id $serverPid -ErrorAction SilentlyContinue
if ($process) {
    $stopped = $false
    $configPath = Join-Path $PSScriptRoot "config.json"
    if (Test-Path -LiteralPath $configPath) {
        try {
            $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
            $headers = @{ Authorization = "Bearer $($config.api_key)" }
            Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$($config.port)/admin/shutdown" -Headers $headers -TimeoutSec 3 | Out-Null
            foreach ($attempt in 1..20) {
                Start-Sleep -Milliseconds 250
                if (-not (Get-Process -Id $serverPid -ErrorAction SilentlyContinue)) {
                    $stopped = $true
                    break
                }
            }
        } catch { }
    }
    if (-not $stopped) {
        try {
            Stop-Process -Id $serverPid -ErrorAction Stop
            $process.WaitForExit(5000) | Out-Null
            $stopped = $true
        } catch {
            throw "Could not stop PID $serverPid. It may have been started elevated or by another account. Open PowerShell as Administrator for this one restart. Original error: $($_.Exception.Message)"
        }
    }
    Write-Host "Stopped Interfayce Remote STT (PID $serverPid)."
} else {
    Write-Host "The recorded server process is no longer running."
}
Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue
