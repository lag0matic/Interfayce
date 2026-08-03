#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'
$reportPath = Join-Path $env:TEMP 'Interfayce-disable-test-mode-completely.txt'
$lines = [System.Collections.Generic.List[string]]::new()

trap {
    $lines.Add("disable_error=$($_.Exception.Message)")
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    exit 1
}

$currentEntry = (& bcdedit /enum '{current}' 2>&1 | Out-String)
$resumeMatch = [regex]::Match(
    $currentEntry,
    '(?im)^resumeobject\s+(\{[0-9a-f-]+\})\s*$'
)
if (-not $resumeMatch.Success) {
    throw 'Could not identify the Windows resume loader entry.'
}
$resumeIdentifier = $resumeMatch.Groups[1].Value

$currentResult = (& bcdedit /set '{current}' testsigning off 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not disable test-signing on the current loader: $currentResult"
}
$resumeResult = (& bcdedit /set $resumeIdentifier testsigning off 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not disable test-signing on the resume loader: $resumeResult"
}

$verification = (& bcdedit /enum all 2>&1 | Out-String)
if ($verification -match '(?im)^testsigning\s+Yes\s*$') {
    throw 'At least one BCD entry still has test-signing enabled.'
}
$lines.Add("current_loader=$currentResult")
$lines.Add("resume_loader=$resumeResult")
$lines.Add('all_test_signing_entries=Off')
$lines.Add('full_restart_required=True')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
