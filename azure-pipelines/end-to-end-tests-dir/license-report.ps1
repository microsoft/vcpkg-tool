. $PSScriptRoot/../end-to-end-tests-prelude.ps1

[string]$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
There are no installed packages, and thus no licenses of installed packages. Did you mean to install something first?

"@

$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-bsd vcpkg-license-mit
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Packages installed in this vcpkg installation declare the following licenses:
BSD-3-Clause
MIT
"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Installed packages declare the following licenses:
BSD-3-Clause
MIT

"@

# Note that the MIT license already is not displayed
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-bsd-on-mit
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Packages installed in this vcpkg installation declare the following licenses:
BSD-3-Clause
"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Installed packages declare the following licenses:
BSD-3-Clause
MIT

"@

# Empty port == no license field set at all
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-port
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
Installed packages declare the following licenses:
BSD-3-Clause
MIT

"@

Run-Vcpkg @commonArgs remove "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-bsd
Throw-IfFailed

# bsd-on-mit is still here so no change
$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
Installed packages declare the following licenses:
BSD-3-Clause
MIT

"@

Run-Vcpkg @commonArgs remove "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-bsd-on-mit vcpkg-license-mit
Throw-IfFailed

# Only unknown left
$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.

"@
