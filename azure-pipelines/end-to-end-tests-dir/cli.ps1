. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Test bad command lines
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "--vcpkg-rootttttt", "C:\"))
Throw-IfNotFailed

Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "--vcpkg-rootttttt=C:\"))
Throw-IfNotFailed

Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "--fast")) # --fast is not a switch
Throw-IfNotFailed

# Test switching stdout vs. stderr for help topics and errors
[string]$out = Run-VcpkgAndCaptureOutput -TestArgs @('help')
Throw-IfFailed
if (-Not ($out.StartsWith('usage: vcpkg <command> [--switches] [--options=values] [arguments] @response_file')))
{
    throw 'Bad help output'
}

$out = Run-VcpkgAndCaptureOutput -TestArgs @('help', 'help')
Throw-IfFailed
if (-Not ($out.StartsWith('Synopsis: Displays specific help topic')))
{
    throw 'Bad help help output'
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('help', 'not-a-topic')
Throw-IfNotFailed
if (-Not ($out.StartsWith('error: unknown topic not-a-topic')))
{
    throw 'Bad help not-a-topic output'
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('not-a-command')
Throw-IfNotFailed
if (-Not ($out.StartsWith('error: invalid command: not-a-command')))
{
    throw 'Bad not-a-command output'
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('install', '--not-a-switch')
Throw-IfNotFailed
if (-Not ($out.StartsWith('error: unexpected switch: --not-a-switch')))
{
    throw 'Bad install --not-a-switch output'
}

if ($IsWindows) {
    $warningText = 'In the September 2023 release'

    # build-external not tested
    # ci not tested
    # export not tested

    # depend-info
    [string]$output = Run-VcpkgAndCaptureStdErr -TestArgs ($directoryArgs + @('depend-info', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if (-Not $output.Contains($warningText)) {
        throw 'depend-info with unqualified spec should emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureStdErr -TestArgs ($directoryArgs + @('depend-info', 'vcpkg-hello-world-1:x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'depend-info with qualified parameters should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureStdErr -TestArgs ($directoryArgs + @('depend-info', 'vcpkg-hello-world-1', '--triplet', 'x86-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'depend-info with arg should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureStdErr -TestArgs ($directoryArgs + @('depend-info', 'vcpkg-hello-world-1', '--triplet', 'x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'depend-info with new default arg should not emit the triplet warning'
    }

    # set-installed
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('x-set-installed'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'x-set-installed with no parameters should not emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('x-set-installed', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if (-Not $output.Contains($warningText)) {
        throw 'x-set-installed with unqualified spec should emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('x-set-installed', 'vcpkg-hello-world-1:x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'x-set-installed with qualified parameters should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('x-set-installed', 'vcpkg-hello-world-1', '--triplet', 'x86-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'x-set-installed with arg should not emit the triplet warning'
    }

    # install
    Refresh-TestRoot
    $sub = Join-Path $TestingRoot 'manifest-warn'
    New-Item -ItemType Directory -Force $sub | Out-Null
    Push-Location $sub
    try {
        Run-Vcpkg -TestArgs ($directoryArgs + @('new', '--application'))
        Throw-IfFailed

        $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('install'))
        Throw-IfFailed
        if (-Not $output.Contains($warningText)) {
            throw 'manifest install should emit the triplet warning'
        }
    } finally {
        Pop-Location
    }

    Refresh-TestRoot
    $output =Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('install', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if (-Not $output.Contains($warningText)) {
        throw 'install with unqualified spec should emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('install', 'vcpkg-hello-world-1:x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'install with qualified parameters should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('install', 'vcpkg-hello-world-1', '--triplet', 'x86-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'install with arg should not emit the triplet warning'
    }

    # upgrade
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('upgrade'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'upgrade with no parameters should not emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('upgrade', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if (-Not $output.Contains($warningText)) {
        throw 'upgrade with unqualified spec should emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('upgrade', 'vcpkg-hello-world-1:x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'upgrade with qualified parameters should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('upgrade', 'vcpkg-hello-world-1', '--triplet', 'x86-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'upgrade with arg should not emit the triplet warning'
    }

    # remove
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('remove', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if (-Not $output.Contains($warningText)) {
        throw 'remove with unqualified spec should emit the triplet warning'
    }
    
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('remove', 'vcpkg-hello-world-1:x64-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'remove with qualified parameters should not emit the triplet warning'
    }

    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('remove', 'vcpkg-hello-world-1', '--triplet', 'x86-windows'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'remove with arg should not emit the triplet warning'
    }

    $env:VCPKG_DEFAULT_TRIPLET = 'x86-windows'
    Refresh-TestRoot
    $output = Run-VcpkgAndCaptureOutput -TestArgs ($directoryArgs + @('install', 'vcpkg-hello-world-1'))
    Throw-IfFailed
    if ($output.Contains($warningText)) {
        throw 'install with environment variable set should not emit the triplet warning'
    }

    Remove-Item env:VCPKG_DEFAULT_TRIPLET
}
