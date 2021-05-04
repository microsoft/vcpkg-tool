$TestingRoot = Join-Path $WorkingRoot 'testing'
$buildtreesRoot = Join-Path $TestingRoot 'buildtrees'
$installRoot = Join-Path $TestingRoot 'installed'
$packagesRoot = Join-Path $TestingRoot 'packages'
$NuGetRoot = Join-Path $TestingRoot 'nuget'
$NuGetRoot2 = Join-Path $TestingRoot 'nuget2'
$ArchiveRoot = Join-Path $TestingRoot 'archives'
$VersionFilesRoot = Join-Path $TestingRoot 'version-test'
$commonArgs = @(
    "--triplet",
    $Triplet,
    "--x-buildtrees-root=$buildtreesRoot",
    "--x-install-root=$installRoot",
    "--x-packages-root=$packagesRoot",
    "--overlay-ports=$PSScriptRoot/e2e_ports/overlays",
    "--overlay-triplets=$PSScriptRoot/e2e_ports/triplets"
)
$Script:CurrentTest = 'unassigned'
$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $TestingRoot 'registries'

function Refresh-TestRoot {
    Remove-Item -Recurse -Force $TestingRoot -ErrorAction SilentlyContinue
    mkdir $TestingRoot | Out-Null
    mkdir $env:X_VCPKG_REGISTRIES_CACHE | Out-Null
    mkdir $NuGetRoot | Out-Null
}

function Write-Stack {
    Get-PSCallStack | % {
        Write-Host "$($_.ScriptName):$($_.ScriptLineNumber): $($_.FunctionName)"
    }
}

function Require-FileExists {
    [CmdletBinding()]
    Param(
        [string]$File
    )
    if (-Not (Test-Path $File)) {
        Write-Stack
        throw "'$Script:CurrentTest' failed to create file '$File'"
    }
}

function Require-FileEquals {
    [CmdletBinding()]
    Param(
        [string]$File,
        [string]$Content
    )
    Require-FileExists $File
    if ((Get-Content $File -Raw) -ne $Content) {
        Write-Stack
        throw "'$Script:CurrentTest' file '$File' did not have the correct contents"
    }
}

function Require-FileNotExists {
    [CmdletBinding()]
    Param(
        [string]$File
    )
    if (Test-Path $File) {
        Write-Stack
        throw "'$Script:CurrentTest' should not have created file '$File'"
    }
}

function Throw-IfFailed {
    if ($LASTEXITCODE -ne 0) {
        Write-Stack
        throw "'$Script:CurrentTest' had a step with a nonzero exit code"
    }
}

function Throw-IfNotFailed {
    if ($LASTEXITCODE -eq 0) {
        Write-Stack
        throw "'$Script:CurrentTest' had a step with an unexpectedly zero exit code"
    }
}

function Write-Trace ([string]$text) {
    Write-Host (@($MyInvocation.ScriptName, ":", $MyInvocation.ScriptLineNumber, ": ", $text) -join "")
}

function Run-Vcpkg {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    $Script:CurrentTest = "$VcpkgExe $($testArgs -join ' ')"
    Write-Host $Script:CurrentTest
    & $VcpkgExe @testArgs
}

Refresh-TestRoot
