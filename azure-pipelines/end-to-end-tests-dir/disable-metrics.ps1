. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Test that metrics are on by default
$metricsTagName = 'vcpkg.disable-metrics'
$metricsAreDisabledMessage = 'warning: passed --sendmetrics, but metrics are disabled.'

function Test-MetricsEnabled() {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )

    $actualArgs = @('version', '--sendmetrics')
    if ($null -ne $TestArgs -and $TestArgs.Length -ne 0) {
        $actualArgs += $TestArgs
    }

    $vcpkgOutput = Run-VcpkgAndCaptureOutput $actualArgs
    if ($vcpkgOutput.Contains($metricsAreDisabledMessage)) {
        Write-Host 'Metrics are disabled'
        return $false
    }

    Write-Host 'Metrics are enabled'
    return $true
}

# By default, metrics are enabled.
Require-FileNotExists $metricsTagName
if (-Not (Test-MetricsEnabled)) {
    throw "Metrics were not on by default."
}

if (Test-MetricsEnabled '--disable-metrics') {
    throw "Metrics were not disabled by switch."
}

$env:VCPKG_DISABLE_METRICS = 'ON'
try {
    if (Test-MetricsEnabled) {
        throw "Environment variable did not disable metrics."
    }

    # Also test that you get no message without --sendmetrics
    $vcpkgOutput = Run-VcpkgAndCaptureOutput list
    if ($vcpkgOutput.Contains($metricsAreDisabledMessage)) {
        throw "Disabled metrics emit message even without --sendmetrics"
    }

    if (-Not (Test-MetricsEnabled '--no-disable-metrics')) {
        throw "Environment variable to disable metrics could not be overridden by switch."
    }
} finally {
    Remove-Item env:VCPKG_DISABLE_METRICS
}

# If the disable-metrics tag file exists, metrics are disabled even if attempted to be enabled on
# the command line.
Set-Content -Path $metricsTagName -Value ""
try {
    if (Test-MetricsEnabled '--disable-metrics') {
        throw "Metrics were not force-disabled by the disable-metrics tag file."
    }
}
finally {
    Remove-Item $metricsTagName
}
