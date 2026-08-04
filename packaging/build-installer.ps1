param(
    [switch]$SkipNativeBuild,
    [switch]$SkipProtocolRefresh
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $PSScriptRoot "out"
$stage = Join-Path $outRoot "stage"
$cache = Join-Path $PSScriptRoot ".cache"
$work = Join-Path $PSScriptRoot ".work"
$protocolRoot = Join-Path $cache "solarxr-protocol"
$protocolCommit = "00c38a6dc28070b30850a89c26b17928e56245d4"
$nodeVersion = "24.15.0"
$appVersion = (Get-Content -LiteralPath (Join-Path $projectRoot "VERSION") -Raw).Trim()

function Reset-GeneratedDirectory([string]$path) {
    $resolvedParent = [IO.Path]::GetFullPath((Split-Path -Parent $path))
    $safeRoot = [IO.Path]::GetFullPath($PSScriptRoot)
    if (-not $resolvedParent.StartsWith($safeRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset a generated directory outside packaging: $path"
    }
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path | Out-Null
}

Push-Location $projectRoot
try {
    if (-not $SkipNativeBuild) {
        if (Get-Process InterfayceOverlay -ErrorAction SilentlyContinue) {
            & "native\build\bin\InterfayceOverlay.exe" --shutdown
            Start-Sleep -Seconds 2
        }
        cmake -S native -B native\build
        cmake --build native\build --config Release
        if ($LASTEXITCODE -ne 0) { throw "Native Release build failed." }
    }

    Reset-GeneratedDirectory $work
    python -m PyInstaller --noconfirm --clean `
        --distpath (Join-Path $work "dist") `
        --workpath (Join-Path $work "pyinstaller") `
        (Join-Path $PSScriptRoot "InterfayceService.spec")
    if ($LASTEXITCODE -ne 0) { throw "PyInstaller build failed." }

    if (-not (Test-Path -LiteralPath $protocolRoot)) {
        New-Item -ItemType Directory -Path $cache -Force | Out-Null
        git clone https://github.com/SlimeVR/SolarXR-Protocol.git $protocolRoot
    }
    if (-not $SkipProtocolRefresh) {
        git -C $protocolRoot fetch --all --tags --prune
        git -C $protocolRoot checkout --detach $protocolCommit
        npm --prefix $protocolRoot ci
        npm --prefix $protocolRoot run build
        if ($LASTEXITCODE -ne 0) { throw "Pinned SolarXR protocol build failed." }
    }

    $nodeArchive = Join-Path $cache "node-v$nodeVersion-win-x64.zip"
    $nodeExtract = Join-Path $cache "node-v$nodeVersion-win-x64"
    if (-not (Test-Path -LiteralPath $nodeExtract)) {
        if (-not (Test-Path -LiteralPath $nodeArchive)) {
            Invoke-WebRequest "https://nodejs.org/dist/v$nodeVersion/node-v$nodeVersion-win-x64.zip" `
                -OutFile $nodeArchive
        }
        Expand-Archive -LiteralPath $nodeArchive -DestinationPath $cache -Force
    }

    Reset-GeneratedDirectory $stage
    Copy-Item native\build\bin\InterfayceOverlay.exe $stage
    Copy-Item native\build\bin\InterfayceAudioEngine.exe $stage
    Copy-Item native\build\bin\openvr_api.dll $stage
    Copy-Item native\build\bin\interfayce.vrmanifest $stage
    Copy-Item native\build\bin\assets $stage -Recurse
    Copy-Item (Join-Path $work "dist\InterfayceService") (Join-Path $stage "service") -Recurse
    New-Item -ItemType Directory -Path (Join-Path $stage "tools\vendor\solarxr-protocol") -Force | Out-Null
    Copy-Item tools\slimevr_probe.cjs,tools\slimevr_reset.cjs (Join-Path $stage "tools")
    Copy-Item (Join-Path $protocolRoot "protocol\typescript\dist") `
        (Join-Path $stage "tools\vendor\solarxr-protocol\protocol\typescript\dist") -Recurse
    Copy-Item (Join-Path $protocolRoot "node_modules\flatbuffers") `
        (Join-Path $stage "tools\vendor\solarxr-protocol\node_modules\flatbuffers") -Recurse
    $protocolLicenses = @(
        (Join-Path $protocolRoot "LICENSE-MIT"),
        (Join-Path $protocolRoot "LICENSE-APACHE")
    )
    Copy-Item -LiteralPath $protocolLicenses `
        -Destination (Join-Path $stage "tools\vendor\solarxr-protocol")
    New-Item -ItemType Directory -Path (Join-Path $stage "runtime") -Force | Out-Null
    Copy-Item (Join-Path $nodeExtract "node.exe"),(Join-Path $nodeExtract "LICENSE") `
        (Join-Path $stage "runtime")
    Copy-Item assets\branding\interfayce-icon-1024.png (Join-Path $stage "Interfayce.png")

    $iscc = @(
        "$env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup 6 is required to build the installer." }
    & $iscc "/DAppVersion=$appVersion" (Join-Path $PSScriptRoot "Interfayce.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }

    Get-FileHash (Join-Path $outRoot "installer\Interfayce-Setup-$appVersion.exe") -Algorithm SHA256
} finally {
    Pop-Location
}
