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
