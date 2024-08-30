. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Refresh-TestRoot

$portsRoot = Join-Path $TestingRoot 'ports'
New-Item -ItemType Directory -Force $portsRoot | Out-Null
Set-EmptyTestPort -Name 'upgrade-test-port' -Version '0' -PortsRoot $portsRoot
$output = Run-VcpkgAndCaptureOutput install upgrade-test-port "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfFailed
if (-Not ($output -match 'upgrade-test-port:[^ ]+@0'))
{
    throw 'Unexpected upgrade-test-port install'
}

Set-EmptyTestPort -Name 'upgrade-test-port' -Version '1' -PortsRoot $portsRoot
$output = Run-VcpkgAndCaptureOutput upgrade "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfNotFailed
if (-Not ($output -match 'If you are sure you want to rebuild the above packages, run this command with the --no-dry-run option.'))
{
    throw "Upgrade didn't handle dry-run correctly"
}

if (-Not ($output -match '\* upgrade-test-port:[^ ]+@1'))
{
    throw "Upgrade didn't choose expected version"
}

$output = Run-VcpkgAndCaptureOutput upgrade --no-dry-run "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfFailed
if (-Not ($output -match '\* upgrade-test-port:[^ ]+@1'))
{
    throw "Upgrade didn't choose expected version"
}

if (-Not ($output -match 'upgrade-test-port:[^:]+: REMOVED:'))
{
    throw "Upgrade didn't remove"
}

if (-Not ($output -match 'upgrade-test-port:[^:]+: SUCCEEDED:'))
{
    throw "Upgrade didn't install"
}

# Also test explicitly providing the name

Set-EmptyTestPort -Name 'upgrade-test-port' -Version '2' -PortsRoot $portsRoot
$output = Run-VcpkgAndCaptureOutput upgrade upgrade-test-port "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfNotFailed
if (-Not ($output -match 'If you are sure you want to rebuild the above packages, run this command with the --no-dry-run option.'))
{
    throw "Upgrade didn't handle dry-run correctly"
}

if (-Not ($output -match '\* upgrade-test-port:[^ ]+@2'))
{
    throw "Upgrade didn't choose expected version"
}

$output = Run-VcpkgAndCaptureOutput upgrade upgrade-test-port --no-dry-run "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfFailed
if (-Not ($output -match '\* upgrade-test-port:[^ ]+@2'))
{
    throw "Upgrade didn't choose expected version"
}

if (-Not ($output -match 'upgrade-test-port:[^:]+: REMOVED:'))
{
    throw "Upgrade didn't remove"
}

if (-Not ($output -match 'upgrade-test-port:[^:]+: SUCCEEDED:'))
{
    throw "Upgrade didn't install"
}

# Also test providing a nonexistent name

$output = Run-VcpkgAndCaptureStdErr upgrade nonexistent "--x-builtin-ports-root=$portsRoot" @commonArgs
Throw-IfNotFailed
if ($output -match 'internal error:')
{
    throw "Upgrade with a nonexistent name crashed"
}
