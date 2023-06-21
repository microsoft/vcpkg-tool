. $PSScriptRoot/../end-to-end-tests-prelude.ps1

try
{
    Copy-Item -Recurse -LiteralPath @(
        "$PSScriptRoot/../e2e_projects/registries-package-patterns",
        "$PSScriptRoot/../e2e_registry"
        ) $WorkingRoot

    $manifestRoot = "$WorkingRoot/registries-package-patterns"
    $e2eRegistryPath = "$WorkingRoot/e2e_registry".Replace('\', '\\')
    Push-Location $e2eRegistryPath
    ### <Initialize registry>
    # Creates a git registry to run the e2e tests on
    try
    {
        Write-Host "Initializing test registry"
        if (Test-Path "$e2eRegistryPath/.git")
        {
            Remove-Item -Recurse -Force "$e2eRegistryPath/.git"
        }
        

        $gitConfig = @(
            '-c', 'user.name=Nobody',
            '-c', 'user.email=nobody@example.com',
            '-c', 'core.autocrlf=false'
        )

        git @gitConfig init . | Out-Null
        Throw-IfFailed
        git @gitConfig add -A | Out-Null
        Throw-IfFailed
        git @gitConfig commit -m "initial commit" | Out-Null
        Throw-IfFailed
        $e2eRegistryBaseline = git rev-parse HEAD
        Throw-IfFailed
    }
    finally 
    {
        Pop-Location
    }
    ### </Initialize Registry>

    function Update-VcpkgJson {
        param($PreReplacementName)
        $content = Get-Content -LiteralPath "$manifestRoot/$PreReplacementName"
        $content = $content.Replace('$E2ERegistryPath', $e2eRegistryPath)
        $content = $content.Replace('$E2ERegistryBaseline', $e2eRegistryBaseline)
        Set-Content -LiteralPath "$manifestRoot/vcpkg.json" -Value $content
    }

    $commonArgs += @("--x-manifest-root=$manifestRoot")

    # [patterns] No patterns (no default)
    Write-Host "[patterns] No patterns (no default)"
    Update-VcpkgJson 'no-patterns.json.in'
    Run-Vcpkg -EndToEndTestSilent @commonArgs install | Out-Null
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Patterns only (no default)
    Write-Host "[patterns] Patterns only (no default)"
    Update-VcpkgJson 'only-patterns.json.in'
    Run-Vcpkg -EndToEndTestSilent @commonArgs install | Out-Null
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Patterns with default
    Write-Host "[patterns] Patterns with default"
    Update-VcpkgJson 'with-default.json.in'
    Run-Vcpkg -EndToEndTestSilent @commonArgs install | Out-Null
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Repeated patterns
    Write-Host "[patterns] Repeated patterns"
    Update-VcpkgJson 'with-redeclaration.json.in'
    $out = Run-VcpkgAndCaptureOutput -EndToEndTestSilent @commonArgs install
    Throw-IfFailed
    if ($out -notmatch "redeclarations will be ignored")
    {
        $out
        throw "Expected warning about redeclaration"
    }
    Refresh-TestRoot
}
finally
{
    Remove-Item -Recurse -Force -LiteralPath @(
        "$WorkingRoot/registries-package-patterns",
        "$WorkingRoot/e2e_registry"
        ) -ErrorAction SilentlyContinue
}
