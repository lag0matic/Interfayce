[CmdletBinding()]
param(
    [switch]$Foreground,
    [switch]$NoWarm
)

$ErrorActionPreference = "Stop"
$kitRoot = $PSScriptRoot
$python = Join-Path $kitRoot ".venv\Scripts\python.exe"
$config = Join-Path $kitRoot "config.json"
$pidFile = Join-Path $kitRoot "server.pid"
$cudaRuntime = Join-Path $kitRoot "cuda-runtime"

if (Test-Path -LiteralPath $cudaRuntime) {
    # The child process inherits this search path. Keeping the redistributable
    # DLLs beside the portable server avoids changing machine-wide PATH.
    $env:PATH = "$cudaRuntime;$env:PATH"
}

if (-not (Test-Path -LiteralPath $python) -or -not (Test-Path -LiteralPath $config)) {
    throw "The server is not installed. Run .\install.ps1 first."
}
if (Test-Path -LiteralPath $pidFile) {
    $oldPid = [int](Get-Content -LiteralPath $pidFile -Raw)
    if (Get-Process -Id $oldPid -ErrorAction SilentlyContinue) {
        Write-Host "Interfayce Remote STT is already running (PID $oldPid)."
        exit 0
    }
    Remove-Item -LiteralPath $pidFile
}

$scriptPath = Join-Path $kitRoot "server.py"
$arguments = @($scriptPath, "--config", $config)
if (-not $NoWarm) { $arguments += "--warm" }
if ($Foreground) {
    & $python @arguments
    exit $LASTEXITCODE
}

$logRoot = Join-Path $kitRoot "logs"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$backgroundArguments = @("`"$scriptPath`"", "--config", "`"$config`"")
if (-not $NoWarm) { $backgroundArguments += "--warm" }
$process = Start-Process -FilePath $python -ArgumentList $backgroundArguments -WorkingDirectory $kitRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logRoot "stdout.log") -RedirectStandardError (Join-Path $logRoot "stderr.log")
$process.Id | Set-Content -LiteralPath $pidFile -Encoding ascii
Start-Sleep -Milliseconds 750
if ($process.HasExited) {
    Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue
    throw "The server exited during startup. Check logs\stderr.log."
}
Write-Host "Interfayce Remote STT started (PID $($process.Id))."
Write-Host "Run .\status.ps1 to check it or .\stop.ps1 to stop it."
