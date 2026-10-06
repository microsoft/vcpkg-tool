if (-not $IsLinux -and -not $IsMacOS) {
    . $PSScriptRoot\..\end-to-end-tests-prelude.ps1

    $project = "$PSScriptRoot\..\e2e-projects\integrate-install-properties\manifest-properties.proj"
    $previousEscapingTest = $env:X_VCPKG_ESCAPING_TEST
    $previousKeepEnvVars = $env:VCPKG_KEEP_ENV_VARS
    try {
        $env:X_VCPKG_ESCAPING_TEST = 'left;right%literal'
        $env:VCPKG_KEEP_ENV_VARS = @(
            $previousKeepEnvVars
            'VCPKG_DOWNLOADS'
            'X_VCPKG_ESCAPING_TEST'
        ) -join ';'

        $msbuildArguments = @(
            $project
            '/nologo'
            '/m:2'
            "/p:SourceIntegrationRoot=$VcpkgRoot\scripts\buildsystems\msbuild\"
            "/p:TestRoot=$TestingRoot\manifest-properties\"
        ) | ForEach-Object { ConvertTo-WindowsCommandLineArgument $_ }

        $output = Run-VcpkgAndCaptureOutput @CommonArgs "--vcpkg-root=$VcpkgRoot" env "msbuild $($msbuildArguments -join ' ')"
        Throw-IfFailed
        Throw-IfNonContains $output 'PASS: local, global, mixed-case, and special-character install properties; unrelated globals removed.'
    }
    finally {
        [System.Environment]::SetEnvironmentVariable('X_VCPKG_ESCAPING_TEST', $previousEscapingTest)
        [System.Environment]::SetEnvironmentVariable('VCPKG_KEEP_ENV_VARS', $previousKeepEnvVars)
    }
}
