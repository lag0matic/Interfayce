[CmdletBinding()]
param(
    [ValidateSet("both", "whisper", "moonshine")]
    [string]$Backend = "both",
    [switch]$Warm,
    [switch]$OpenFirewall,
    [switch]$NoAutoStart
)

$ErrorActionPreference = "Stop"
$kitRoot = $PSScriptRoot
$venvPython = Join-Path $kitRoot ".venv\Scripts\python.exe"

function Find-Python {
    foreach ($version in @("3.12", "3.11")) {
        try {
            $null = & py "-$version" -c "import sys; print(sys.executable)" 2>$null
            if ($LASTEXITCODE -eq 0) { return @("py", "-$version") }
        } catch { }
    }
    throw "Python 3.11 or 3.12 was not found. Install it from https://www.python.org/downloads/windows/ and run this script again."
}

if (-not (Test-Path -LiteralPath $venvPython)) {
    $python = Find-Python
    Write-Host "Creating private Python environment..."
    $launcher = $python[0]
    & $launcher $python[1] -m venv (Join-Path $kitRoot ".venv")
}

& $venvPython -m pip install --upgrade pip
& $venvPython -m pip install -r (Join-Path $kitRoot "requirements-base.txt")
if ($Backend -in @("both", "whisper")) {
    & $venvPython -m pip install -r (Join-Path $kitRoot "requirements-faster-whisper.txt")
}
if ($Backend -in @("both", "moonshine")) {
    & $venvPython -m pip install -r (Join-Path $kitRoot "requirements-moonshine.txt")
}

$configPath = Join-Path $kitRoot "config.json"
if (-not (Test-Path -LiteralPath $configPath)) {
    $config = Get-Content -LiteralPath (Join-Path $kitRoot "config.example.json") -Raw | ConvertFrom-Json
    $keyBytes = New-Object byte[] 32
    $random = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $random.GetBytes($keyBytes)
    } finally {
        $random.Dispose()
    }
    $config.api_key = [BitConverter]::ToString($keyBytes).Replace("-", "").ToLowerInvariant()
    if ($Backend -eq "whisper") {
        $config.models.PSObject.Properties.Remove("moonshine")
    } elseif ($Backend -eq "moonshine") {
        $config.models.PSObject.Properties.Remove("whisper-turbo")
        $config.default_model = "moonshine"
    }
    $config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8
    Write-Host "Created config.json with a random API key."
} else {
    Write-Host "Keeping existing config.json."
}

& $venvPython (Join-Path $kitRoot "diagnose.py") --config $configPath
if ($LASTEXITCODE -ne 0) {
    throw "Diagnostics failed. Review the messages above."
}

if ($OpenFirewall) {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "-OpenFirewall requires an elevated PowerShell window. The server itself does not require admin rights."
    }
    $port = (Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json).port
    New-NetFirewallRule -DisplayName "Interfayce Remote STT" -Direction Inbound -Action Allow -Protocol TCP -LocalPort $port -Profile Private -ErrorAction SilentlyContinue | Out-Null
    Write-Host "Opened private-network TCP port $port."
}

if ($Warm) {
    Write-Host "Downloading and warming configured models. This can take several minutes."
    $warmArguments = @((Join-Path $kitRoot "server.py"), "--config", $configPath, "--warm-only")
    if ($Backend -eq "both") {
        $warmArguments += @("--model", "whisper-turbo", "--model", "moonshine")
    }
    & $venvPython @warmArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Model warm-up failed. Check the CUDA note in README.md and retry."
    }
}

if (-not $NoAutoStart) {
    & (Join-Path $kitRoot "autostart.ps1") install
}

Write-Host ""
Write-Host "Installation complete. Run .\start.ps1"
Write-Host "The API key is in config.json; copy that value into the Interfayce STT settings."
