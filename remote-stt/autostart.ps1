[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("install", "remove", "status")]
    [string]$Action = "install"
)

$ErrorActionPreference = "Stop"
$shortcutName = "Interfayce Remote STT.lnk"
$startupDirectory = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startupDirectory $shortcutName

if ($Action -eq "status") {
    if (Test-Path -LiteralPath $shortcutPath) {
        Write-Host "Interfayce Remote STT auto-start is enabled."
        Write-Host $shortcutPath
        exit 0
    }
    Write-Host "Interfayce Remote STT auto-start is disabled."
    exit 1
}

if ($Action -eq "remove") {
    if (Test-Path -LiteralPath $shortcutPath) {
        Remove-Item -LiteralPath $shortcutPath
        Write-Host "Removed Interfayce Remote STT from Windows login."
    } else {
        Write-Host "Interfayce Remote STT auto-start was already disabled."
    }
    exit 0
}

$startScript = Join-Path $PSScriptRoot "start.ps1"
if (-not (Test-Path -LiteralPath $startScript)) {
    throw "start.ps1 was not found beside autostart.ps1."
}

New-Item -ItemType Directory -Path $startupDirectory -Force | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$shortcut.Arguments = "-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$startScript`""
$shortcut.WorkingDirectory = $PSScriptRoot
$shortcut.Description = "Start the Interfayce Remote STT server silently"
$shortcut.WindowStyle = 7
$shortcut.Save()

Write-Host "Interfayce Remote STT will start silently at Windows login."
Write-Host "Run '.\autostart.ps1 remove' to disable it."
