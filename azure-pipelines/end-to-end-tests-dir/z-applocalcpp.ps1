if ($IsWindows) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1

    # Path to my simple project
    $buildDir = "$PSScriptRoot/../e2e_projects/applocal-test/build"

    Run-Vcpkg env "`"$buildDir/build.bat`" `"$buildDir`""

    # Tests z-applocal command
    Run-Vcpkg z-applocal `
            --target-binary=$buildDir/main.exe `
            --installed-bin-dir=$buildDir
    Throw-IfFailed

    # Tests z-applocal command with no arguments
    Run-Vcpkg z-applocal
    Throw-IfNotFailed

    # Tests z-applocal with no installed-bin-dir argument
    Run-Vcpkg z-applocal `
            --target-binary=$buildDir/main.exe
    Throw-IfNotFailed

    # Tests z-applocal with no target-binary argument
    Run-Vcpkg z-applocal `
            --installed-bin-dir=$buildDir
    Throw-IfNotFailed
}
