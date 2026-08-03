#Requires -RunAsAdministrator

$reportPath = Join-Path $env:TEMP 'Interfayce-test-mode-inspection.txt'
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("timestamp=$([DateTime]::Now.ToString('o'))")
$os = Get-CimInstance Win32_OperatingSystem
$lines.Add("last_boot=$($os.LastBootUpTime.ToString('o'))")
$lines.Add('bcdedit_all_begin')
$lines.Add((& bcdedit /enum all 2>&1 | Out-String).Trim())
$lines.Add('bcdedit_all_end')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
