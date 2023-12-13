. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

git clone $VcpkgRoot "$TestingRoot/temp-repo" --local
try
{
    $env:VCPKG_ROOT = "$TestingRoot/temp-repo"
    git -C "$TestingRoot/temp-repo" switch -d e1934f4a2a0c58bb75099d89ed980832379907fa # vcpkg-cmake @ 2022-12-22
    $output = Run-VcpkgAndCaptureOutput install vcpkg-cmake
    Throw-IfFailed
    if (-Not ($output -match 'vcpkg-cmake:[^ ]+@2022-12-22'))
    {
        throw 'Unexpected vcpkg-cmake install'
    }

    git -C "$TestingRoot/temp-repo" switch -d f6a5d4e8eb7476b8d7fc12a56dff300c1c986131 # vcpkg-cmake @ 2023-05-04
    $output = Run-VcpkgAndCaptureOutput upgrade
    Throw-IfNotFailed
    if (-Not ($output -match 'If you are sure you want to rebuild the above packages, run this command with the --no-dry-run option.'))
    {
        throw "Upgrade didn't handle dry-run correctly"
    }

    if (-Not ($output -match '\* vcpkg-cmake:[^ ]+@2023-05-04'))
    {
        throw "Upgrade didn't choose expected version"
    }

    $output = Run-VcpkgAndCaptureOutput upgrade --no-dry-run
    Throw-IfFailed
    if (-Not ($output -match '\* vcpkg-cmake:[^ ]+@2023-05-04'))
    {
        throw "Upgrade didn't choose expected version"
    }

    if (-Not ($output -match 'vcpkg-cmake:[^:]+: REMOVED:'))
    {
        throw "Upgrade didn't remove"
    }

    if (-Not ($output -match 'vcpkg-cmake:[^:]+: SUCCEEDED:'))
    {
        throw "Upgrade didn't install"
    }
}
finally
{
    $env:VCPKG_ROOT = $VcpkgRoot
}
