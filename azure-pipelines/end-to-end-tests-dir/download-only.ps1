. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Refresh-TestRoot

Run-Vcpkg @commonArgs install vcpkg-uses-touch
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/vcpkg-uses-touch.h"
