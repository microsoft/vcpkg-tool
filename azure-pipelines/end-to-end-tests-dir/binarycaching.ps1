. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @(
    "--host-triplet",
    $Triplet
)

# Test simple installation
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config", "--binarycaching", "--x-binarysource=clear;files,$ArchiveRoot,write"))
Throw-IfFailed

# Test simple removal
Run-Vcpkg -TestArgs ($commonArgs + @("remove", "vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config"))
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/include"

if(-Not $IsLinux -and -Not $IsMacOS) {
    # Test simple nuget installation
    Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config", "--binarycaching", "--x-binarysource=clear;nuget,$NuGetRoot,readwrite"))
    Throw-IfFailed
}

# Test restoring from files archive
Remove-Item -Recurse -Force $installRoot
Remove-Item -Recurse -Force $buildtreesRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install","vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config","--binarycaching","--x-binarysource=clear;files,$ArchiveRoot,read"))
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
Require-FileNotExists "$buildtreesRoot/vcpkg-hello-world-1/src"
Require-FileExists "$buildtreesRoot/detect_compiler"

# Test --no-binarycaching
Remove-Item -Recurse -Force $installRoot
Remove-Item -Recurse -Force $buildtreesRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install","vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config","--no-binarycaching","--x-binarysource=clear;files,$ArchiveRoot,read"))
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
Require-FileExists "$buildtreesRoot/vcpkg-hello-world-1/src"
Require-FileExists "$buildtreesRoot/detect_compiler"

# Test --editable
Remove-Item -Recurse -Force $installRoot
Remove-Item -Recurse -Force $buildtreesRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install","vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config","--editable","--x-binarysource=clear;files,$ArchiveRoot,read"))
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
Require-FileExists "$buildtreesRoot/vcpkg-hello-world-1/src"
Require-FileNotExists "$buildtreesRoot/detect_compiler"

if(-Not $IsLinux -and -Not $IsMacOS) {
    # Test restoring from nuget
    Remove-Item -Recurse -Force $installRoot
    Remove-Item -Recurse -Force $buildtreesRoot
    Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config", "--binarycaching", "--x-binarysource=clear;nuget,$NuGetRoot"))
    Throw-IfFailed
    Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
    Require-FileNotExists "$buildtreesRoot/vcpkg-hello-world-1/src"

    # Test four-phase flow
    Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
    Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "vcpkg-cmake", "vcpkg-cmake-config", "--dry-run", "--x-write-nuget-packages-config=$TestingRoot/packages.config"))
    Throw-IfFailed
    Require-FileNotExists "$installRoot/$Triplet/include/hello-1.h"
    Require-FileNotExists "$buildtreesRoot/vcpkg-hello-world-1/src"
    Require-FileExists "$TestingRoot/packages.config"
    $fetchNuGetArgs = $commonArgs + @('fetch', 'nuget')
    if ($IsLinux -or $IsMacOS) {
        mono $(Run-Vcpkg @fetchNuGetArgs) restore $TestingRoot/packages.config -OutputDirectory "$NuGetRoot2" -Source "$NuGetRoot"
    } else {
        & $(Run-VcpkgAndCaptureOutput @fetchNuGetArgs) restore $TestingRoot/packages.config -OutputDirectory "$NuGetRoot2" -Source "$NuGetRoot"
    }
    Throw-IfFailed
    Remove-Item -Recurse -Force $NuGetRoot -ErrorAction SilentlyContinue
    mkdir $NuGetRoot
    Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "vcpkg-hello-world-2", "vcpkg-cmake", "vcpkg-cmake-config", "--binarycaching", "--x-binarysource=clear;nuget,$NuGetRoot2;nuget,$NuGetRoot,write"))
    Throw-IfFailed
    Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
    Require-FileExists "$installRoot/$Triplet/include/hello-2.h"
    Require-FileNotExists "$buildtreesRoot/vcpkg-hello-world-1/src"
    Require-FileExists "$buildtreesRoot/vcpkg-hello-world-2/src"
    if ((Get-ChildItem $NuGetRoot -Filter '*.nupkg' | Measure-Object).Count -ne 1) {
        throw "In '$CurrentTest': did not create exactly 1 NuGet package"
    }

    # Test export
    $CurrentTest = 'Exporting'
    Require-FileNotExists "$TestingRoot/vcpkg-export-output"
    Require-FileNotExists "$TestingRoot/vcpkg-export.1.0.0.nupkg"
    Require-FileNotExists "$TestingRoot/vcpkg-export-output.zip"
    Run-Vcpkg -TestArgs ($commonArgs + @("export", "vcpkg-hello-world-1", "vcpkg-hello-world-2", "vcpkg-cmake", "vcpkg-cmake-config", "--nuget", "--nuget-id=vcpkg-export", "--nuget-version=1.0.0", "--output=vcpkg-export-output", "--raw", "--zip", "--output-dir=$TestingRoot"))
    Require-FileExists "$TestingRoot/vcpkg-export-output"
    Require-FileExists "$TestingRoot/vcpkg-export.1.0.0.nupkg"
    Require-FileExists "$TestingRoot/vcpkg-export-output.zip"
}
