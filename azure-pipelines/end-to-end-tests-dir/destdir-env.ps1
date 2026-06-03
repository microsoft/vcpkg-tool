. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if ($IsLinux) {
    $CurrentTest = "DESTDIR Environment Sanitization"

    $previousDestDir = [System.Environment]::GetEnvironmentVariable("DESTDIR")
    try {
        $env:DESTDIR = (Join-Path $TestingRoot "destdir-should-not-leak")
        Refresh-TestRoot

        Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-destdir-env-check --no-binarycaching
        Throw-IfFailed
    }
    finally {
        if ($null -eq $previousDestDir) {
            Remove-Item Env:\DESTDIR -ErrorAction SilentlyContinue
        }
        else {
            $env:DESTDIR = $previousDestDir
        }
    }
}
