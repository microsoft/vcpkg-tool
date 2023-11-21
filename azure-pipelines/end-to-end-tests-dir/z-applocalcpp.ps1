if ($IsWindows) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1

    Refresh-TestRoot
    Copy-Item -Recurse -LiteralPath "$PSScriptRoot/../e2e-projects/applocal" -Destination $TestingRoot
    
    # Tests basic z-applocal command
    $basicDir = "$TestingRoot/applocal/basic"
    Run-Vcpkg env "$basicDir/build.bat"
    Require-FileNotExists $basicDir/mylib.dll
    $applocalOutput = Run-VcpkgAndCaptureOutput z-applocal `
            --target-binary=$basicDir/main.exe `
            --installed-bin-dir=$basicDir/installed/bin
    Throw-IfFailed
    if (-Not ($applocalOutput -match '.*\\applocal\\basic\\installed\\bin\\mylib\.dll -> .*\\applocal\\basic\\mylib\.dll.*'))
    {
        throw "z-applocal didn't copy dependent binary"
    }

    Require-FileExists $basicDir/mylib.dll

    # Tests z-applocal command with no arguments
    Run-Vcpkg z-applocal
    Throw-IfNotFailed

    # Tests z-applocal with no installed-bin-dir argument
    Run-Vcpkg z-applocal `
            --target-binary=$basicDir/main.exe
    Throw-IfNotFailed

    # Tests z-applocal with no target-binary argument
    Run-Vcpkg z-applocal `
            --installed-bin-dir=$basicDir
    Throw-IfNotFailed

    # Tests deploy azure kinect sensor SDK plugins
    $pluginsDir = "$TestingRoot/applocal/plugins"
    Run-Vcpkg env "$pluginsDir/build.bat"
    Require-FileNotExists "$pluginsDir/k4a.dll"
    Require-FileNotExists "$pluginsDir/depthengine_2_0.dll"
    $applocalOutput = Run-VcpkgAndCaptureOutput z-applocal `
           --target-binary=$pluginsDir/main.exe `
           --installed-bin-dir=$pluginsDir/installed/bin
    Throw-IfFailed
    if (-Not ($applocalOutput -match '.*\\applocal\\plugins\\installed\\bin\\k4a\.dll -> .*\\applocal\\plugins\\k4a\.dll.*'))
    {
        throw "z-applocal didn't copy dependent binary"
    }

    if (-Not ($applocalOutput -match '.*\\applocal\\plugins\\installed\\tools\\azure-kinect-sensor-sdk\\depthengine_2_0\.dll -> .*\\applocal\\plugins\\depthengine_2_0\.dll.*'))
    {
        throw "z-applocal didn't copy xbox plugins"
    }

    Require-FileExists "$pluginsDir/k4a.dll"
    Require-FileExists "$pluginsDir/depthengine_2_0.dll"

    # Tests deploy azure kinect sensor SDK plugins from debug directories
    $pluginsDebugDir = "$TestingRoot/applocal/plugins-debug"
    Run-Vcpkg env "$pluginsDebugDir/build.bat"
    Require-FileNotExists "$pluginsDebugDir/k4a.dll"
    Require-FileNotExists "$pluginsDebugDir/depthengine_2_0.dll"
    $applocalOutput = Run-VcpkgAndCaptureOutput z-applocal `
           --target-binary=$pluginsDebugDir/main.exe `
           --installed-bin-dir=$pluginsDebugDir/installed/debug/bin
    Throw-IfFailed
    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\debug\\bin\\k4a\.dll -> .*\\applocal\\plugins-debug\\k4a\.dll.*'))
    {
        throw "z-applocal didn't copy dependent debug binary"
    }

    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\tools\\azure-kinect-sensor-sdk\\depthengine_2_0\.dll -> .*\\applocal\\plugins-debug\\depthengine_2_0\.dll.*'))
    {
        throw "z-applocal didn't copy xbox plugins"
    }

    Require-FileExists "$pluginsDir/k4a.dll"
    Require-FileExists "$pluginsDir/depthengine_2_0.dll"

    # Tests that nonexistent files are merely warnings
    $nonexistentDll = Join-Path $basicDir 'nonexisting.dll'
    Require-FileNotExists $nonexistentDll
    $nonexistentOutput = Run-VcpkgAndCaptureOutput z-applocal `
        --target-binary=$nonexistentDll `
        --installed-bin-dir=$basicDir/installed/bin
    Throw-IfNotFailed
    if ($nonexistentOutput -match 'error:')
    {
        throw "Nonexistent emitted an error"
    }

    if ($nonexistentOutput -notmatch 'warning: no such file or directory')
    {
        throw "Nonexistent didn't emit expected warning"
    }

    # Tests that static libs emit a 'does not appear to be executable' warning
    $staticLibDir = "$TestingRoot/applocal/static-lib"
    Run-Vcpkg env "$staticLibDir/build.bat"
    $staticLibFile = "$staticLibDir/static-lib.lib"
    Require-FileExists $staticLibFile
    $staticLibOutput = Run-VcpkgAndCaptureOutput z-applocal `
        --target-binary=$staticLibFile `
        --installed-bin-dir=$basicDir/installed/bin
    Throw-IfNotFailed
    if ($staticLibOutput -match 'error:')
    {
        throw "Static library emitted an error"
    }

    if ($staticLibOutput -notmatch 'warning: this file does not appear to be executable')
    {
        throw "Static library didn't emit expected warning"
    }
}
