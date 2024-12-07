Set-StrictMode -Version Latest

[string]$poshVcpkgModulePath = "$PSScriptRoot/../../scripts/posh-vcpkg.psd1"
[System.IO.FileInfo]$vcpkgExe = $VcpkgItem

[string]$TestSpecsDir = "$PSScriptRoot/../e2e-specs"

$containerPosh = New-PesterContainer -Path @(
    "$TestSpecsDir/autocomplete-posh-vcpkg.Tests.ps1"
) -Data @(
    @{ 
        poshVcpkgModulePath = $poshVcpkgModulePath
        vcpkgExe            = $vcpkgExe
    }
)

if (-not (Test-Path variable:IsWindows)) { Set-Variable IsWindows $true }

$configuration = [PesterConfiguration]@{
    Run    = @{
        Container = @(
            $containerPosh
        )
    }
    Output = @{
        Verbosity = 'Detailed'
    }
    Filter = @{
        ExcludeTag = @{
            NonWindowsOnly = -not $IsWindows
            WindowsOnly    = $IsWindows
            CoreOnly       = 'Core' -eq $PSEdition
        }.GetEnumerator().Where{ -not $_.Value }.ForEach{ $_.Name }
    }
}

Invoke-Pester -Configuration $configuration
