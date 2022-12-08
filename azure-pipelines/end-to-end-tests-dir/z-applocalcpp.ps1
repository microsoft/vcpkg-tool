if ($IsWindows) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1

    # Path to my simple project
    $buildDir = "$PSScriptRoot/../e2e_projects/applocal-test/build"

    # Tests z-applocal command no logs
    Run-Vcpkg z-applocal `
            --target-binary=$buildDir/Debug/main.exe `
            --installed-bin-dir=$buildDir/x64-windows/debug/bin
    Throw-IfFailed

    # Tests z-applocal command with logs
    Run-Vcpkg z-applocal `
            --target-binary=$buildDir/Debug/main.exe `
            --installed-bin-dir=$buildDir/x64-windows/debug/bin `
            --tlog-file=$buildDir/logs/vcpkg.applocal.tlog `
            --copied-files-log=$buildDir/logs/vcpkg.applocal.log
    Throw-IfFailed

    # Tests z-applocal command with no arguments
    Run-Vcpkg z-applocal
    Throw-IfNotFailed

    # Tests z-applocal with no installed-bin-dir argument
    Run-Vcpkg z-applocal `
            --target-binary=$buildDir/Debug/main.exe
    Throw-IfNotFailed

    # Tests z-applocal with no target-binary argument
    Run-Vcpkg z-applocal `
            --installed-bin-dir=$buildDir/x64-windows/debug/bin
    Throw-IfNotFailed
}
