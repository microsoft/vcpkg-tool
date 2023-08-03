. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Refresh-TestRoot

Run-Vcpkg @commonArgs install --only-downloads vcpkg-uses-touch
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/vcpkg-uses-touch.h"

Refresh-TestRoot

Run-Vcpkg @commonArgs install --only-downloads vcpkg-uses-touch-missing-dependency
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/include/vcpkg-uses-touch.h"
