. $PSScriptRoot/../end-to-end-tests-prelude.ps1

try
{
    Copy-Item -Recurse -LiteralPath @(
        "$PSScriptRoot/../e2e-projects/e2e-registry-templates",
        "$PSScriptRoot/../e2e-registry"
        ) $WorkingRoot

    $manifestRoot = "$WorkingRoot/e2e-registry-templates"
    $e2eRegistryPath = "$WorkingRoot/e2e-registry".Replace('\', '\\')
    Push-Location $e2eRegistryPath
    ### <Initialize registry>
    # Creates a git registry to run the e2e tests on
    try
    {
        Write-Host "Initializing test registry"
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

    # Testing registries' package selection patterns
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
    Run-Vcpkg @commonArgs install
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Patterns only (no default)
    Write-Host "[patterns] Patterns only (no default)"
    Update-VcpkgJson 'only-patterns.json.in'
    Run-Vcpkg @commonArgs install
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Patterns with default
    Write-Host "[patterns] Patterns with default"
    Update-VcpkgJson 'with-default.json.in'
    Run-Vcpkg @commonArgs install
    Throw-IfFailed
    Refresh-TestRoot

    # [patterns] Repeated patterns
    Write-Host "[patterns] Repeated patterns"
    Update-VcpkgJson 'with-redeclaration.json.in'
    $out = Run-VcpkgAndCaptureOutput @commonArgs install
    Throw-IfFailed
    if ($out -notmatch "redeclarations will be ignored")
    {
        throw 'Expected warning about redeclaration'
    }

    Refresh-TestRoot

    # Testing that overrides can select ports that are removed from the baseline
    Write-Host "[removed] Removed from baseline"
    Update-VcpkgJson 'removed.json.in'
    $out = Run-VcpkgAndCaptureOutput @commonArgs install
    Throw-IfFailed
    if ($out -match 'error: the baseline does not contain an entry for port removed' -Or
        $out -notmatch 'The following packages will be built and installed:\s+removed:[^ ]+@1.0.0 -- git\+[^\n]+@9b82c31964570870d27a5bb634f5b84e13f8b90a'
        )
    {
        throw 'Baseline removed port could not be selected with overrides'
    }

    Refresh-TestRoot
}
finally
{
    Remove-Item -Recurse -Force -LiteralPath @(
        "$WorkingRoot/e2e-registry-templates",
        "$WorkingRoot/e2e-registry"
        ) -ErrorAction SilentlyContinue
}
