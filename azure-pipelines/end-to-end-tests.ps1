# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: MIT
#
<#
.SYNOPSIS
End-to-End tests for the vcpkg executable.

.DESCRIPTION
These tests cover the command line interface and broad functions of vcpkg, including `install`, `remove` and certain
binary caching scenarios. They use the vcpkg executable in the current directory.

.PARAMETER Triplet
The triplet to use for testing purposes.

.PARAMETER WorkingRoot
The location used as scratch space for testing.

#>

[CmdletBinding()]
Param(
    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string]$WorkingRoot = 'work',
    [Parameter(Mandatory = $false)]
    [string]$VcpkgRoot,
    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string]$Filter,
    [Parameter(Mandatory = $false)]
    [string]$StartAt,
    [Parameter(Mandatory = $false)]
    [string]$VcpkgExe,
    [Parameter(Mandatory = $false, HelpMessage="Run artifacts tests, only usable when vcpkg was built with VCPKG_ARTIFACTS_DEVELOPMENT=ON")]
    [switch]$RunArtifactsTests
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if ($PSVersionTable.PSVersion.Major -lt 7) {
    Write-Error "vcpkg end to end tests must use pwsh rather than Windows PowerShell"
}

# If you get an error on the next line, install Pester from an administrative command prompt with:
# Install-Module -Name Pester -Force -MinimumVersion '5.6.1' -MaximumVersion '5.99' -Scope AllUsers
Import-Module Pester -Force -MinimumVersion '5.6.1' -MaximumVersion '5.99'

if ($IsLinux) {
    $Triplet = 'x64-linux'
} elseif ($IsMacOS) {
    $Triplet = 'x64-osx'
} else {
    $Triplet = 'x86-windows'
}

New-Item -Path $WorkingRoot -ItemType Directory -Force | Out-Null
$WorkingRoot = (Get-Item $WorkingRoot).FullName
if ([string]::IsNullOrWhitespace($VcpkgRoot)) {
    $VcpkgRoot = $env:VCPKG_ROOT
}

if ([string]::IsNullOrWhitespace($VcpkgRoot)) {
    Write-Error "Could not determine VCPKG_ROOT"
    throw
}

$VcpkgRoot = (Get-Item $VcpkgRoot).FullName

[string]$executableExtension = ''
if ($IsWindows)
{
    $executableExtension = '.exe'
}

if ([string]::IsNullOrEmpty($VcpkgExe))
{
    $VcpkgExe = "./vcpkg$executableExtension"
}

$VcpkgItem = Get-Item $VcpkgExe
$VcpkgExe = $VcpkgItem.FullName
$VcpkgPs1 = Join-Path $VcpkgItem.Directory "vcpkg-shell.ps1"
$TestScriptAssetCacheExe = Join-Path $VcpkgItem.Directory "test-script-asset-cache"
$TestSuitesDir = Join-Path $PSScriptRoot "end-to-end-tests-dir"

[System.IO.FileInfo[]]$AllTests = Get-ChildItem -LiteralPath $TestSuitesDir -File -Filter "*.ps1" | Sort-Object -Property Name
if ($null -ne $Filter) {
    $AllTests = $AllTests | Where-Object Name -Match $Filter
}

$envvars_clear = @(
    'VCPKG_BINARY_SOURCES',
    'VCPKG_DEFAULT_HOST_TRIPLET',
    'VCPKG_DEFAULT_TRIPLET',
    'VCPKG_DISABLE_METRICS',
    'VCPKG_FEATURE_FLAGS',
    'VCPKG_FORCE_DOWNLOADED_BINARIES',
    'VCPKG_FORCE_SYSTEM_BINARIES',
    'VCPKG_KEEP_ENV_VARS',
    'VCPKG_OVERLAY_PORTS',
    'VCPKG_OVERLAY_TRIPLETS',
    'VCPKG_ROOT',
    'X_VCPKG_ASSET_SOURCES'
)
$envvars = $envvars_clear + @("VCPKG_DOWNLOADS", "X_VCPKG_REGISTRIES_CACHE", "PATH", "GITHUB_ACTIONS")

$allTestsCount = $AllTests.Count
for ($n = 1; $n -le $allTestsCount; $n++)
{
    $Test = $AllTests[$n - 1]
    $testDisplayName = [System.IO.Path]::GetRelativePath($TestSuitesDir, $Test.FullName)
    if ($StartAt.Length -ne 0) {
        [string]$TestName = $Test.Name
        $TestName = $TestName.Substring(0, $TestName.Length - 4) # remove .ps1
        if ($StartAt.Equals($TestName, [System.StringComparison]::OrdinalIgnoreCase)) {
            $StartAt = [string]::Empty
        } else {
            Write-Host -ForegroundColor Green "[end-to-end-tests.ps1] [$n/$allTestsCount] Suite $testDisplayName skipped by -StartAt"
            continue
        }
    }

    if ($env:GITHUB_ACTIONS) {
        Write-Host -ForegroundColor Green "::group::[end-to-end-tests.ps1] [$n/$allTestsCount] Running suite $testDisplayName"
    } else {
        Write-Host -ForegroundColor Green "[end-to-end-tests.ps1] [$n/$allTestsCount] Running suite $testDisplayName"
    }

    $envbackup = @{}
    foreach ($var in $envvars)
    {
        $envbackup[$var] = [System.Environment]::GetEnvironmentVariable($var)
    }

    [int]$lastTestExitCode = 0
    try
    {
        foreach ($var in $envvars_clear)
        {
            if (Test-Path "Env:\$var")
            {
                Remove-Item "Env:\$var"
            }
        }
        $env:VCPKG_ROOT = $VcpkgRoot
        & $Test
        $lastTestExitCode = $LASTEXITCODE
    }
    finally
    {
        foreach ($var in $envvars)
        {
            if ($null -eq $envbackup[$var])
            {
                if (Test-Path "Env:\$var")
                {
                    Remove-Item "Env:\$var"
                }
            }
            else
            {
                Set-Item "Env:\$var" $envbackup[$var]
            }
        }
    }
    if ($env:GITHUB_ACTIONS) {
        Write-Host "::endgroup::"
    }

    if ($lastTestExitCode -ne 0)
    {
        Write-Error "[end-to-end-tests.ps1] Suite $testDisplayName failed with exit code $lastTestExitCode"
    }
}

Write-Host -ForegroundColor Green "[end-to-end-tests.ps1] All tests passed."
