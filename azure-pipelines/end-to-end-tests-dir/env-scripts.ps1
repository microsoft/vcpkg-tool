. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Run-Vcpkg @directoryArgs "--overlay-triplets=$PSScriptRoot/../e2e-ports/env-scripts" "--overlay-ports=$PSScriptRoot/../e2e-ports/env-scripts" x-set-installed test-env-scripts --triplet env-scripts --binarysource=clear --debug
Throw-IfFailed
