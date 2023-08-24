if (-not $IsLinux -and -not $IsMacOS) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1
    $iiroot = "$PSScriptRoot/../e2e-ports/integrate-install"

    $env:VCPKG_BINARY_SOURCES="clear;default,read"
    $env:VCPKG_KEEP_ENV_VARS="VCPKG_KEEP_ENV_VARS;VCPKG_BINARY_SOURCES;VCPKG_FORCE_SYSTEM_BINARIES;VCPKG_DOWNLOADS;VCPKG_DEFAULT_BINARY_CACHE"

    # Test msbuild props and targets
    $Script:CurrentTest = "zlib:x86-windows msbuild $iiroot\..."
    Write-Host $Script:CurrentTest
    Run-Vcpkg @CommonArgs integrate install
    Run-Vcpkg @CommonArgs install zlib:x86-windows
    Throw-IfFailed
    foreach ($project in @("Project1", "NoProps")) {
        $Script:CurrentTest = "msbuild $iiroot\$project.vcxproj"
        Write-Host $Script:CurrentTest
        Run-Vcpkg @commonArgs env "msbuild $iiroot\$project.vcxproj /p:VCPKG_ROOT=$VcpkgRoot /p:VcpkgRoot=$TestingRoot /p:IntDir=$TestingRoot\int\ /p:OutDir=$TestingRoot\out\ "
        Throw-IfFailed
        Remove-Item -Recurse -Force $TestingRoot\int
        Remove-Item -Recurse -Force $TestingRoot\out
    }

    $Script:CurrentTest = "zlib:x86-windows-static msbuild $iiroot\..."
    Write-Host $Script:CurrentTest
    Run-Vcpkg @CommonArgs install zlib:x86-windows-static
    Throw-IfFailed
    foreach ($project in @("VcpkgTriplet", "VcpkgTriplet2", "VcpkgUseStatic", "VcpkgUseStatic2")) {
        $Script:CurrentTest = "msbuild $iiroot\$project.vcxproj"
        Run-Vcpkg @commonArgs env "msbuild $iiroot\$project.vcxproj /p:VCPKG_ROOT=$VcpkgRoot /p:VcpkgRoot=$TestingRoot /p:IntDir=$TestingRoot\int\ /p:OutDir=$TestingRoot\out\ "
        Throw-IfFailed
        Remove-Item -Recurse -Force $TestingRoot\int
        Remove-Item -Recurse -Force $TestingRoot\out
    }

    # This test is currently disabled because it requires adding the ability to override the vcpkg executable into the msbuild props/targets.
    # Require-FileNotExists $installRoot/x64-windows-static/include/zlib.h
    # Require-FileNotExists $installRoot/x64-windows/include/zlib.h
    # Require-FileExists $installRoot/x86-windows/include/zlib.h
    # $Script:CurrentTest = "msbuild $iiroot\VcpkgUseStaticManifestHost.vcxproj"
    # ./vcpkg $commonArgs env "msbuild $iiroot\VcpkgUseStaticManifestHost.vcxproj /p:VCPKG_ROOT=$VcpkgRoot `"/p:_VcpkgExecutable=$VcpkgExe`" /p:VcpkgRoot=$TestingRoot /p:IntDir=$TestingRoot\int\ /p:OutDir=$TestingRoot\out\ /p:TestingVcpkgInstalledDir=$installRoot"
    # Throw-IfFailed
    # Require-FileExists $installRoot/x64-windows-static/include/zlib.h
    # Require-FileNotExists $installRoot/x86-windows/include/zlib.h
}
