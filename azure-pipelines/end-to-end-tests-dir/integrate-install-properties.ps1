if (-not $IsLinux -and -not $IsMacOS) {
    . $PSScriptRoot\..\end-to-end-tests-prelude.ps1

    $project = "$PSScriptRoot\..\e2e-projects\integrate-install-properties\manifest-properties.proj"
    $msbuildArguments = @(
        $project
        '/nologo'
        '/m:2'
        "/p:SourceIntegrationRoot=$VcpkgRoot\scripts\buildsystems\msbuild\"
        "/p:TestRoot=$TestingRoot\manifest-properties\"
    ) | ForEach-Object { ConvertTo-WindowsCommandLineArgument $_ }

    $previousEnvironmentValue = [System.Environment]::GetEnvironmentVariable('VCPKG_TEST_INSTALL_PROPERTIES')
    $previousKeepEnvironmentVariables = [System.Environment]::GetEnvironmentVariable('VCPKG_KEEP_ENV_VARS')
    try {
        $env:VCPKG_TEST_INSTALL_PROPERTIES = 'semicolon;literal%3Bpercent%25'
        $env:VCPKG_KEEP_ENV_VARS = 'VCPKG_TEST_INSTALL_PROPERTIES'
        $output = Run-VcpkgAndCaptureOutput @CommonArgs "--vcpkg-root=$VcpkgRoot" env "msbuild $($msbuildArguments -join ' ')"
        Throw-IfFailed
    }
    finally {
        [System.Environment]::SetEnvironmentVariable('VCPKG_TEST_INSTALL_PROPERTIES', $previousEnvironmentValue)
        [System.Environment]::SetEnvironmentVariable('VCPKG_KEEP_ENV_VARS', $previousKeepEnvironmentVariables)
    }

    foreach ($case in @('global', 'local', 'mixed-case', 'special-characterssemicolon;percent%apostrophe''paren()dollar$at@')) {
        Throw-IfNonContains $output "Validated install properties for $TestingRoot\manifest-properties\$case\"
    }
    Throw-IfNonContains $output 'PASS: local, global, mixed-case, and special-character install properties; unrelated globals removed.'
}
