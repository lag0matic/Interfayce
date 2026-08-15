[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$AudioDirectory,
    [int]$Repeat = 2,
    [string]$Server = "http://127.0.0.1",
    [string[]]$Model
)
$python = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python)) { throw "Run .\install.ps1 first." }
$arguments = @((Join-Path $PSScriptRoot "benchmark.py"), $AudioDirectory,
    "--config", (Join-Path $PSScriptRoot "config.json"), "--repeat", $Repeat,
    "--server", $Server)
foreach ($name in $Model) { $arguments += @("--model", $name) }
& $python @arguments
