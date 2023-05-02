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
    [string]$VcpkgExe,
    [Parameter(Mandatory = $false, HelpMessage="Run artifacts tests, only usable when vcpkg was built with VCPKG_ARTIFACTS_DEVELOPMENT=ON")]
    [switch]$RunArtifactsTests
)

$ErrorActionPreference = "Stop"

if ($PSVersionTable.PSVersion.Major -lt 7) {
    Write-Error "vcpkg end to end tests must use pwsh rather than Windows PowerShell"
}

if ($IsLinux) {
    $Triplet = 'x64-linux'
} elseif ($IsMacOS) {
    $Triplet = 'x64-osx'
} else {
    $Triplet = 'x86-windows'
}

New-Item -Path $WorkingRoot -ItemType Directory -Force
$WorkingRoot = (Get-Item $WorkingRoot).FullName
if ([string]::IsNullOrWhitespace($VcpkgRoot)) {
    $VcpkgRoot = $env:VCPKG_ROOT
}

if ([string]::IsNullOrWhitespace($VcpkgRoot)) {
    Write-Error "Could not determine VCPKG_ROOT"
    throw
}

$VcpkgRoot = (Get-Item $VcpkgRoot).FullName

if ([string]::IsNullOrEmpty($VcpkgExe))
{
    if ($IsWindows)
    {
        $VcpkgExe = Get-Item './vcpkg.exe'
    }
    else
    {
        $VcpkgExe = Get-Item './vcpkg'
    }
}

$VcpkgExe = (Get-Item $VcpkgExe).FullName
$VcpkgPs1 = Join-Path ((Get-Item $VcpkgExe).Directory) "vcpkg.ps1"

[Array]$AllTests = Get-ChildItem $PSScriptRoot/end-to-end-tests-dir/*.ps1
if ($Filter -ne $Null) {
    $AllTests = $AllTests | ? { $_.Name -match $Filter }
}
$n = 1
$m = $AllTests.Count

$envvars_clear = @(
    "VCPKG_FORCE_SYSTEM_BINARIES",
    "VCPKG_FORCE_DOWNLOADED_BINARIES",
    "VCPKG_DEFAULT_HOST_TRIPLET",
    "VCPKG_DEFAULT_TRIPLET",
    "VCPKG_BINARY_SOURCES",
    "VCPKG_OVERLAY_PORTS",
    "VCPKG_OVERLAY_TRIPLETS",
    "VCPKG_KEEP_ENV_VARS",
    "VCPKG_ROOT",
    "VCPKG_FEATURE_FLAGS",
    "VCPKG_DISABLE_METRICS"
)
$envvars = $envvars_clear + @("VCPKG_DOWNLOADS", "X_VCPKG_REGISTRIES_CACHE", "PATH")

foreach ($Test in $AllTests)
{
    if ($env:GITHUB_ACTIONS) {
        Write-Host -ForegroundColor Green "::group::[end-to-end-tests.ps1] [$n/$m] Running suite $Test"
    } else {
        Write-Host -ForegroundColor Green "[end-to-end-tests.ps1] [$n/$m] Running suite $Test"
    }

    $envbackup = @{}
    foreach ($var in $envvars)
    {
        $envbackup[$var] = [System.Environment]::GetEnvironmentVariable($var)
    }

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
    }
    finally
    {
        foreach ($var in $envvars)
        {
            if ($envbackup[$var] -eq $null)
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
    $n += 1
}

Write-Host -ForegroundColor Green "[end-to-end-tests.ps1] All tests passed."
$global:LASTEXITCODE = 0
