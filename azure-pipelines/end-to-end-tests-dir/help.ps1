. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$Script:CurrentTest = "help topics"
$output = Run-VcpkgAndCaptureOutput help topics
Throw-IfFailed
$topics = $output -split "`n" | Select-Object -Skip 1 | ForEach-Object { $_.Trim() } | Where-Object { $_ }
foreach ($topic in $topics) {
    Run-Vcpkg help $topic
    Throw-IfFailed
}

$Script:CurrentTest = "privacy notice uses forwarding link"
$privacyNoticeOutput = Run-VcpkgAndCaptureOutput help privacy
Throw-IfFailed
if (-not $privacyNoticeOutput.Contains("https://go.microsoft.com/fwlink/?LinkId=521839")) {
    throw "Privacy notice output does not contain the expected forwarding link."
}

$Script:CurrentTest = "telemetry notice uses docs link"
$telemetryNoticeOutput = Run-VcpkgAndCaptureOutput help telemetry
Throw-IfFailed
if (-not $telemetryNoticeOutput.Contains("https://learn.microsoft.com/vcpkg/about/privacy")) {
    throw "Telemetry notice output does not contain the expected docs link."
}
