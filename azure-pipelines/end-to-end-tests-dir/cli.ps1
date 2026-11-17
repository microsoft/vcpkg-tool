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

# A closed stdout pipe should terminate vcpkg without aborting.
$ports = Get-ChildItem -LiteralPath (Join-Path $VcpkgRoot 'ports') -Directory |
    Select-Object -First 500 -ExpandProperty Name
$responseFile = Join-Path $WorkingRoot 'closed-stdout-pipe.rsp'
@("--vcpkg-root=$VcpkgRoot", 'x-package-info', '--x-json') + $ports |
    Set-Content -LiteralPath $responseFile -Encoding utf8
$closesStdinExe = Join-Path (Split-Path -Parent $VcpkgExe) "closes-stdin$executableExtension"
if ($IsWindows)
{
    $command = "`"$VcpkgExe`" `"@$responseFile`" 2>`"$stderrFile`" | `"$closesStdinExe`""
    & $env:COMSPEC /d /c $command
}
else
{
    & /bin/sh -c '"$0" "@$1" 2>"$2" | "$3"' $VcpkgExe $responseFile $stderrFile $closesStdinExe
}

$stderr = Get-Content -LiteralPath $stderrFile -Raw
if (-Not [string]::IsNullOrEmpty($stderr))
{
    throw "Writing to a closed stdout pipe produced stderr: $stderr"
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

$out = Run-VcpkgAndCaptureOutput -TestArgs @('install', 'this-is-super-not-a-#port')
Throw-IfNotFailed

[string]$expected = @"
error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: this-is-super-not-a-#port
                                     ^

"@

if (-Not ($out.EndsWith($expected)))
{
    throw 'Bad malformed port name output; it was: ' + $out
}

$out = Run-VcpkgAndCaptureOutput -TestArgs @('install', 'zlib', '--binarysource=clear;not-a-backend')
Throw-IfNotFailed

$expected = @"
error: unknown binary provider type: valid providers are 'clear', 'default', 'nuget', 'nugetconfig', 'nugettimeout', 'interactive', 'x-azblob', 'x-gcs', 'x-aws', 'x-aws-config', 'http', and 'files'
  on expression: clear;not-a-backend
                       ^

"@

if (-Not ($out.EndsWith($expected)))
{
    throw 'Bad malformed --binarysource output; it was: ' + $out
}

$out = Run-VcpkgAndCaptureOutput -TestArgs @('install', 'zlib', '--triplet', 'ARM-windows')
Throw-IfNotFailed

$expected = @"
error: Invalid triplet name. Triplet names are all lowercase alphanumeric+hyphens.
  on expression: ARM-windows
                 ^
Built-in Triplets:
"@

if (-Not ($out.StartsWith($expected)))
{
    throw 'Bad malformed triplet output. It was: ' + $out
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('x-package-info', 'zlib#notaport', '--x-json', '--x-installed')
Throw-IfNotFailed
$expected = @"
error: expected an explicit triplet
  on expression: zlib#notaport
                     ^

"@
if (-Not ($out.EndsWith($expected)))
{
    throw ('Bad error output: ' + $out)
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('x-package-info', 'zlib', '--x-json', '--x-installed')
Throw-IfNotFailed
$expected = @"
error: expected an explicit triplet
  on expression: zlib
                     ^

"@
if (-Not ($out.EndsWith($expected)))
{
    throw ('Bad error output: ' + $out)
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('x-package-info', 'zlib:x64-windows[core]', '--x-json', '--x-installed')
Throw-IfNotFailed
$expected = @"
error: expected the end of input parsing a package spec; did you mean zlib[core]:x64-windows instead?
  on expression: zlib:x64-windows[core]
                                 ^

"@
if (-Not ($out.EndsWith($expected)))
{
    throw ('Bad error output: ' + $out)
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('x-package-info', 'zlib[core]:x64-windows', '--x-json', '--x-installed')
Throw-IfNotFailed
$expected = @"
error: List of features is not allowed in this context
  on expression: zlib[core]:x64-windows
                     ^

"@
if (-Not ($out.EndsWith($expected)))
{
    throw ('Bad error output: ' + $out)
}

$out = Run-VcpkgAndCaptureStdErr -TestArgs @('x-package-info', 'zlib:x64-windows(windows)', '--x-json', '--x-installed')
Throw-IfNotFailed
$expected = @"
error: Platform qualifier is not allowed in this context
  on expression: zlib:x64-windows(windows)
                                 ^

"@
if (-Not ($out.EndsWith($expected)))
{
    throw ('Bad error output: ' + $out)
}
