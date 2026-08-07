[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDirectory
)

$ErrorActionPreference = "Stop"
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destination = Join-Path $PSScriptRoot "cuda-runtime"
$patterns = @("cublas*.dll", "cudnn*.dll")
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$copied = @()
foreach ($pattern in $patterns) {
    $files = Get-ChildItem -LiteralPath $source -Filter $pattern -File -Recurse
    foreach ($file in $files) {
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        $copied += $file.Name
    }
}

$required = @("cublas64_12.dll", "cublasLt64_12.dll", "cudnn_ops64_9.dll")
$missing = @($required | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $destination $_))
})
if ($missing.Count) {
    throw "The source did not contain required runtime files: $($missing -join ', ')"
}
Write-Host "Imported $($copied.Count) CUDA/cuDNN runtime files into cuda-runtime."
Write-Host "Restart the STT server to use them."
