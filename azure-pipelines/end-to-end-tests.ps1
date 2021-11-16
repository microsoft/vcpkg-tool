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
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Triplet,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WorkingRoot,
    [Parameter(Mandatory = $false)]
    [string]$VcpkgRoot,
    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string]$Filter,
    [Parameter(Mandatory = $false)]
    [string]$VcpkgExe
)

$ErrorActionPreference = "Stop"

if (-Not (Test-Path $WorkingRoot)) {
    New-Item -Path $WorkingRoot -ItemType Directory
}

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

[Array]$AllTests = Get-ChildItem $PSScriptRoot/end-to-end-tests-dir/*.ps1
if ($Filter -ne $Null) {
    $AllTests = $AllTests | ? { $_.Name -match $Filter }
}
$n = 1
$m = $AllTests.Count

$envvars_clear = @(
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
$envvars = $envvars_clear + @("VCPKG_DOWNLOADS", "X_VCPKG_REGISTRIES_CACHE")

foreach ($Test in $AllTests)
{
    Write-Host "[end-to-end-tests.ps1] [$n/$m] Running suite $Test"

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
                Set-Item "Env:\$var" "$envbackup[$var]"
            }
        }
    }
    $n += 1
}

Write-Host "[end-to-end-tests.ps1] All tests passed."
$LASTEXITCODE = 0
