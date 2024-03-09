. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Refresh-TestRoot

Run-Vcpkg @commonArgs install --only-downloads vcpkg-uses-touch
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/vcpkg-uses-touch.h"

Refresh-TestRoot

Run-Vcpkg @commonArgs install --only-downloads vcpkg-uses-touch-missing-dependency
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/include/vcpkg-uses-touch.h"

Refresh-TestRoot

Run-Vcpkg @commonArgs install "--overlay-ports=$PSScriptRoot/../e2e-ports" vcpkg-e2e-test-fails-in-download-only-transitive --only-downloads
Throw-IfFailed
if (Test-Path "$installRoot/$Triplet/share/vcpkg-e2e-test-fails-in-download-only-transitive/installed.txt") {
	throw "--only-downloads installed a port with a missing transitive dependency"
}


Run-Vcpkg @commonArgs install "--overlay-ports=$PSScriptRoot/../e2e-ports" vcpkg-e2e-test-fails-in-download-only-transitive
Throw-IfFailed
if (-Not (Test-Path "$installRoot/$Triplet/share/vcpkg-e2e-test-fails-in-download-only-transitive/installed.txt")) {
	throw "The --only-downloads transitive test port did not succeed (likely test bug)"
}
