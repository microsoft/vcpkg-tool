. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$expectedPrivacy = "Read the Microsoft Privacy Statement at https://go.microsoft.com/fwlink/?LinkId=521839`n"
$actualPrivacy = Run-VcpkgAndCaptureOutput help privacy
Throw-IfFailed
if ($actualPrivacy -cne $expectedPrivacy) {
    throw "help privacy output did not match.`nExpected: $expectedPrivacy`nActual: $actualPrivacy"
}

$expectedTelemetry = @'
vcpkg collects usage data in order to help us improve your experience.
The data collected by Microsoft is anonymous.
You can opt-out of telemetry by re-running the bootstrap-vcpkg script with -disableMetrics, passing --disable-metrics to vcpkg on the command line, or by setting the VCPKG_DISABLE_METRICS environment variable.

Read more about vcpkg telemetry at https://learn.microsoft.com/vcpkg/about/privacy
Read the Microsoft Privacy Statement at https://go.microsoft.com/fwlink/?LinkId=521839
'@ -replace "`r`n", "`n"
$expectedTelemetry += "`n"

$actualTelemetry = Run-VcpkgAndCaptureOutput help telemetry
Throw-IfFailed
if ($actualTelemetry -cne $expectedTelemetry) {
    throw "help telemetry output did not match.`nExpected: $expectedTelemetry`nActual: $actualTelemetry"
}

$topics = Run-VcpkgAndCaptureOutput help topics
Throw-IfFailed
foreach ($topic in @('privacy', 'telemetry')) {
    if (-not $topics.Contains($topic)) {
        throw "$topic was not listed as a help topic"
    }
}

Run-Vcpkg privacy-notice
Throw-IfNotFailed
