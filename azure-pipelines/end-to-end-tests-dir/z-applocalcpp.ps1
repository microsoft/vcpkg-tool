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

    # Tests deploy Qt 5 and Qt 6 libraries and plugins when both versions are installed
    $qtDir = "$TestingRoot/applocal/qt"
    Run-Vcpkg env "$qtDir/build.bat"

    $qtCases = @(
        @{
            Name = "Qt 5 release"
            Major = 5
            Suffix = ""
            AppDir = "qt5-release"
            InstalledBinDir = "installed/bin"
            PluginsDir = "installed/plugins"
            OtherPluginsDir = "installed/Qt6/plugins"
        },
        @{
            Name = "Qt 5 debug"
            Major = 5
            Suffix = "d"
            AppDir = "qt5-debug"
            InstalledBinDir = "installed/debug/bin"
            PluginsDir = "installed/debug/plugins"
            OtherPluginsDir = "installed/debug/Qt6/plugins"
        },
        @{
            Name = "Qt 6 release"
            Major = 6
            Suffix = ""
            AppDir = "qt6-release"
            InstalledBinDir = "installed/bin"
            PluginsDir = "installed/Qt6/plugins"
            OtherPluginsDir = "installed/plugins"
        },
        @{
            Name = "Qt 6 debug"
            Major = 6
            Suffix = "d"
            AppDir = "qt6-debug"
            InstalledBinDir = "installed/debug/bin"
            PluginsDir = "installed/debug/Qt6/plugins"
            OtherPluginsDir = "installed/debug/plugins"
        }
    )

    foreach ($qtCase in $qtCases)
    {
        $appDir = Join-Path $qtDir $qtCase.AppDir
        $installedBinDir = Join-Path $qtDir $qtCase.InstalledBinDir
        $coreLibrary = "Qt$($qtCase.Major)Core$($qtCase.Suffix).dll"
        $guiLibrary = "Qt$($qtCase.Major)Gui$($qtCase.Suffix).dll"
        $networkLibrary = "Qt$($qtCase.Major)Network$($qtCase.Suffix).dll"
        $printSupportLibrary = "Qt$($qtCase.Major)PrintSupport$($qtCase.Suffix).dll"
        $sqlLibrary = "Qt$($qtCase.Major)Sql$($qtCase.Suffix).dll"
        $widgetsLibrary = "Qt$($qtCase.Major)Widgets$($qtCase.Suffix).dll"
        $imageFormatPlugin = "qjpeg$($qtCase.Suffix).dll"
        $platformPlugin = "qwindows$($qtCase.Suffix).dll"
        $otherSuffix = if ($qtCase.Suffix) { "" } else { "d" }
        $otherImageFormatPlugin = "qjpeg$otherSuffix.dll"
        $otherPlatformPlugin = "qwindows$otherSuffix.dll"
        $copiedFilesLog = Join-Path $appDir "copied-files.log"
        $pluginFiles = @(
            "imageformats/$imageFormatPlugin",
            "platforms/$platformPlugin",
            "printsupport/qprint$($qtCase.Suffix).dll",
            "sqldrivers/qsqlite$($qtCase.Suffix).dll",
            "styles/qstyle$($qtCase.Suffix).dll"
        )
        if ($qtCase.Major -eq 5)
        {
            $pluginFiles += "bearer/qbearer$($qtCase.Suffix).dll"
        }
        else
        {
            $pluginFiles += @(
                "generic/qgeneric$($qtCase.Suffix).dll",
                "networkinformation/qnetworkinformation$($qtCase.Suffix).dll",
                "platformthemes/qplatformtheme$($qtCase.Suffix).dll",
                "tls/qtls$($qtCase.Suffix).dll"
            )
        }

        Require-FileNotExists (Join-Path $appDir $coreLibrary)
        Require-FileNotExists (Join-Path $appDir $guiLibrary)
        Require-FileNotExists (Join-Path $appDir $networkLibrary)
        Require-FileNotExists (Join-Path $appDir $printSupportLibrary)
        Require-FileNotExists (Join-Path $appDir $sqlLibrary)
        Require-FileNotExists (Join-Path $appDir $widgetsLibrary)
        Require-FileNotExists (Join-Path $appDir "qt.conf")
        foreach ($pluginFile in $pluginFiles)
        {
            Require-FileNotExists (Join-Path $appDir "plugins/$pluginFile")
        }

        Run-Vcpkg z-applocal `
            "--target-binary=$(Join-Path $appDir 'main.exe')" `
            "--installed-bin-dir=$installedBinDir" `
            "--copied-files-log=$copiedFilesLog"
        Throw-IfFailed
        Require-FileExists (Join-Path $appDir $coreLibrary)
        Require-FileExists (Join-Path $appDir $guiLibrary)
        Require-FileExists (Join-Path $appDir $networkLibrary)
        Require-FileExists (Join-Path $appDir $printSupportLibrary)
        Require-FileExists (Join-Path $appDir $sqlLibrary)
        Require-FileExists (Join-Path $appDir $widgetsLibrary)
        Require-FileExists (Join-Path $appDir "qt.conf")
        foreach ($pluginFile in $pluginFiles)
        {
            Require-FileExists (Join-Path $appDir "plugins/$pluginFile")
        }

        Require-FileNotExists (Join-Path $appDir "plugins/imageformats/$otherImageFormatPlugin")
        Require-FileNotExists (Join-Path $appDir "plugins/platforms/$otherPlatformPlugin")
        if ($qtCase.Major -eq 5)
        {
            Require-FileNotExists (Join-Path $appDir "plugins/generic/qgeneric$($qtCase.Suffix).dll")
            Require-FileNotExists (Join-Path $appDir "plugins/platformthemes/qplatformtheme$($qtCase.Suffix).dll")
            Require-FileNotExists (Join-Path $appDir "plugins/tls/qtls$($qtCase.Suffix).dll")
            Require-FileExists (Join-Path $appDir "libssl-unused.dll")
        }
        else
        {
            Require-FileNotExists (Join-Path $appDir "plugins/bearer/qbearer$($qtCase.Suffix).dll")
            Require-FileNotExists (Join-Path $appDir "libssl-unused.dll")
        }

        if ((Get-Content -Raw -LiteralPath (Join-Path $appDir "qt.conf")) -ne "[Paths]`n")
        {
            throw "z-applocal generated an unexpected $($qtCase.Name) qt.conf"
        }

        $copiedFiles = Get-Content -LiteralPath $copiedFilesLog
        $expectedPluginSource = Join-Path (Join-Path $qtDir $qtCase.PluginsDir) "platforms/$platformPlugin"
        $unexpectedPluginSource = Join-Path (Join-Path $qtDir $qtCase.OtherPluginsDir) "platforms/$platformPlugin"
        if ($copiedFiles -notcontains $expectedPluginSource)
        {
            throw "z-applocal didn't deploy the $($qtCase.Name) platform plugin from $expectedPluginSource"
        }

        if ($copiedFiles -contains $unexpectedPluginSource)
        {
            throw "z-applocal deployed the $($qtCase.Name) platform plugin from $unexpectedPluginSource"
        }
    }

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

    # Tests deploy azure kinect sensor SDK plugins from release and debug directories when both are present
    $pluginsDebugDir = "$TestingRoot/applocal/plugins-debug"
    Run-Vcpkg env "$pluginsDebugDir/build.bat"
    Require-FileNotExists "$pluginsDebugDir/k4a.dll"
    Require-FileNotExists "$pluginsDebugDir/depthengine_2_0.dll"

    $applocalOutput = Run-VcpkgAndCaptureOutput z-applocal `
           --target-binary=$pluginsDebugDir/main.exe `
           --installed-bin-dir=$pluginsDebugDir/installed/bin
    Throw-IfFailed
    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\bin\\k4a\.dll -> .*\\applocal\\plugins-debug\\k4a\.dll.*'))
    {
        throw "z-applocal didn't copy dependent release binary"
    }

    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\tools\\azure-kinect-sensor-sdk\\depthengine_2_0\.dll -> .*\\applocal\\plugins-debug\\depthengine_2_0\.dll.*'))
    {
        throw "z-applocal didn't copy release xbox plugins"
    }

    Require-FileExists "$pluginsDebugDir/k4a.dll"
    Require-FileExists "$pluginsDebugDir/depthengine_2_0.dll"
    Remove-Item -LiteralPath "$pluginsDebugDir/k4a.dll", "$pluginsDebugDir/depthengine_2_0.dll"

    $applocalOutput = Run-VcpkgAndCaptureOutput z-applocal `
           --target-binary=$pluginsDebugDir/main.exe `
           --installed-bin-dir=$pluginsDebugDir/installed/debug/bin
    Throw-IfFailed
    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\debug\\bin\\k4a\.dll -> .*\\applocal\\plugins-debug\\k4a\.dll.*'))
    {
        throw "z-applocal didn't copy dependent debug binary"
    }

    if (-Not ($applocalOutput -match '.*\\applocal\\plugins-debug\\installed\\debug\\tools\\azure-kinect-sensor-sdk\\depthengine_2_0\.dll -> .*\\applocal\\plugins-debug\\depthengine_2_0\.dll.*'))
    {
        throw "z-applocal didn't copy debug xbox plugins"
    }

    Require-FileExists "$pluginsDebugDir/k4a.dll"
    Require-FileExists "$pluginsDebugDir/depthengine_2_0.dll"

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
