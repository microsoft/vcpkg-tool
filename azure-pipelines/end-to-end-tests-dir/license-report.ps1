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

# Note that the MIT license from the already installed package is not displayed for the install
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

# Installing another port that says BSD-3-clause like bsd-on-mit results in no change to the license report
Run-Vcpkg @commonArgs remove "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-bsd
Throw-IfFailed

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

# Only unknown from vcpkg-empty-port left
$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.

"@

Run-Vcpkg @commonArgs remove vcpkg-empty-port
Throw-IfFailed

# Test that license ANDed together are broken onto separate lines
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-apache-and-boost
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected  @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Packages installed in this vcpkg installation declare the following licenses:
Apache-2.0
BSL-1.0

"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Installed packages declare the following licenses:
Apache-2.0
BSL-1.0

"@

Run-Vcpkg @commonArgs remove vcpkg-license-apache-and-boost
Throw-IfFailed

# Test that license reporting responds to features

# Install without specifying features (should behave like empty port)
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-license-features
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.

"@

# Install apache-2 and boost features
Run-Vcpkg @commonArgs remove vcpkg-license-features
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" "vcpkg-license-features[apache-2,boost]"
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfContains -Actual $output -Expected @"
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
"@
Throw-IfNonContains -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Installed packages declare the following licenses:
Apache-2.0
BSL-1.0

"@

# Install bsd-3-clause, mit, and null features (null should trigger warning)
Run-Vcpkg @commonArgs remove vcpkg-license-features
$output = Run-VcpkgAndCaptureOutput @commonArgs install "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" "vcpkg-license-features[bsd-3-clause,mit,null]"
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
Packages installed in this vcpkg installation declare the following licenses:
BSD-3-Clause
MIT

"@

$output = Run-VcpkgAndCaptureOutput @commonArgs license-report "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"
Throw-IfFailed
Throw-IfNonContains -Actual $output -Expected @"
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the ``copyright`` file for each package for more information about their licensing.
Installed packages declare the following licenses:
BSD-3-Clause
MIT

"@

Run-Vcpkg @commonArgs remove vcpkg-license-features
Throw-IfFailed
