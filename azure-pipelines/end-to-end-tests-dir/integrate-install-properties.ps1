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

    $output = Run-VcpkgAndCaptureOutput @CommonArgs "--vcpkg-root=$VcpkgRoot" env "msbuild $($msbuildArguments -join ' ')"
    Throw-IfFailed
    Throw-IfNonContains $output 'PASS: local, global, mixed-case, and special-character install properties; unrelated globals removed.'
}
