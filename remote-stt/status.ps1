$ErrorActionPreference = "Stop"
$configPath = Join-Path $PSScriptRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) { throw "Run .\install.ps1 first." }
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$headers = @{ Authorization = "Bearer $($config.api_key)" }
$lastError = $null
foreach ($attempt in 1..10) {
    try {
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$($config.port)/health" -Headers $headers -TimeoutSec 2
        $health | ConvertTo-Json -Depth 6
        exit 0
    } catch {
        $lastError = $_.Exception.Message
        if ($attempt -lt 10) { Start-Sleep -Milliseconds 500 }
    }
}
Write-Error "Server did not answer after startup retries: $lastError"
