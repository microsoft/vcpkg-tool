. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (-not $IsMacOS -and -not $IsLinux) {
    "" | Out-File -enc ascii $(Join-Path $TestingRoot .vcpkg-root)

    $Scripts = Join-Path $TestingRoot "scripts"
    mkdir $Scripts | Out-Null

    $7zip_version = "24.08"
    $ninja_version = "1.11.1"

    $env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'down loads'
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "7zip", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/7zip-${7zip_version}-windows/7za.exe"

    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows/ninja.exe"

    mkdir "$TestingRoot/down loads/tools/ninja-testing-${ninja_version}-windows" | Out-Null
    Move-Item -Path "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows/ninja.exe" -Destination "$TestingRoot/down loads/tools/ninja-testing-${ninja_version}-windows/ninja.exe"
    $path = $env:PATH

    $env:PATH = "$path;$TestingRoot/down loads/tools/ninja-testing-${ninja_version}-windows"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows/ninja.exe"

    $env:VCPKG_FORCE_DOWNLOADED_BINARIES = "1"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows/ninja.exe"

    Remove-Item -Recurse -Force "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows" -ErrorAction SilentlyContinue
    Remove-Item env:VCPKG_FORCE_DOWNLOADED_BINARIES

    $env:VCPKG_FORCE_SYSTEM_BINARIES = "1"
    $env:PATH = "$PSScriptRoot\..\e2e-assets\fetch;$path"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$TestingRoot/down loads/tools/ninja-${ninja_version}-windows/ninja.exe"

    Remove-Item env:VCPKG_FORCE_SYSTEM_BINARIES
    $out = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot", "--x-stderr-status"))
    Throw-IfFailed
    & $out --version
    if ($LASTEXITCODE -ne 0) {
        throw 'Couldn''t run resulting ninja'
    }
}
