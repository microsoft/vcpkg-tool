if ($IsWindows) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1

    # Paths to test projects
    $buildDir = "$PSScriptRoot/../e2e_projects/applocal-test/build"
    $pluginsDir = "$PSScriptRoot/../e2e_projects/applocal-test/plugins"

    Run-Vcpkg env "$buildDir/build.bat"
    Run-Vcpkg env "$pluginsDir/azure_kinect_sensor_sdk/build.bat"

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

    # Tests deploy azure kinect sensor SDK plugins
    Run-Vcpkg z-applocal `
           --target-binary=$pluginsDir/azure_kinect_sensor_sdk/main.exe `
           --installed-bin-dir=$pluginsDir/azure_kinect_sensor_sdk
     Throw-IfFailed
}
