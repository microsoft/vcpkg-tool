$TestingRoot = Join-Path $WorkingRoot 'testing'
$buildtreesRoot = Join-Path $TestingRoot 'buildtrees'
$installRoot = Join-Path $TestingRoot 'installed'
$packagesRoot = Join-Path $TestingRoot 'packages'
$NuGetRoot = Join-Path $TestingRoot 'nuget'
$NuGetRoot2 = Join-Path $TestingRoot 'nuget2'
$ArchiveRoot = Join-Path $TestingRoot 'archives'
$VersionFilesRoot = Join-Path $TestingRoot 'version-test'
$directoryArgs = @(
    "--x-buildtrees-root=$buildtreesRoot",
    "--x-install-root=$installRoot",
    "--x-packages-root=$packagesRoot",
    "--overlay-ports=$PSScriptRoot/e2e-ports/overlays",
    "--overlay-triplets=$PSScriptRoot/e2e-ports/triplets"
)

$commonArgs = @(
    "--triplet",
    $Triplet
) + $directoryArgs

$gitConfigOptions = @(
  '-c', 'user.name=Nobody',
  '-c', 'user.email=nobody@example.com',
  '-c', 'core.autocrlf=false'
)

$stderrFile = Join-Path $WorkingRoot 'last-stderr.txt'

$Script:CurrentTest = 'unassigned'

function Refresh-TestRoot {
    Remove-Item -Recurse -Force $TestingRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $TestingRoot | Out-Null
    New-Item -ItemType Directory -Force $NuGetRoot | Out-Null
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

function Require-JsonFileEquals {
    [CmdletBinding()]
    Param(
        [string]$File,
        [string]$Json
    )
    Require-FileExists $File
    $ExpectedJson = $Json | ConvertFrom-Json | ConvertTo-Json -Compress
    $ActualJson = Get-Content $File | ConvertFrom-Json | ConvertTo-Json -Compress

    if ($ActualJson -ne $ExpectedJson) {
        Write-Stack
        throw "'$Script:CurrentTest' file '$File' did not have the correct contents`n
                Expected: $ExpectedJson`n
                Actual:   $ActualJson"
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
    [CmdletBinding()]
    Param(
        [string]$Message = ""
    )
    if ($LASTEXITCODE -ne 0) {
        Write-Stack
        throw "'$Script:CurrentTest' had a step with a nonzero exit code: $Message"
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

function Run-VcpkgAndCaptureOutput {
    Param(
        [Parameter(Mandatory = $false)]
        [Switch]$ForceExe,

        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    $thisVcpkg = $VcpkgPs1;
    if ($ForceExe) {
        $thisVcpkg = $VcpkgExe;
    }

    $Script:CurrentTest = "$thisVcpkg $($testArgs -join ' ')"
    Write-Host -ForegroundColor red $Script:CurrentTest
    $result = (& "$thisVcpkg" @testArgs) | Out-String
    Write-Host -ForegroundColor Gray $result
    $result
}

function Run-VcpkgAndCaptureStdErr {
    Param(
        [Parameter(Mandatory = $false)]
        [Switch]$ForceExe,

        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    $thisVcpkg = $VcpkgPs1;
    if ($ForceExe) {
        $thisVcpkg = $VcpkgExe;
    }

    $Script:CurrentTest = "$thisVcpkg $($testArgs -join ' ')"
    Write-Host -ForegroundColor red $Script:CurrentTest
    & "$thisVcpkg" @testArgs 2> $stderrFile
    Get-Content -LiteralPath $stderrFile -Encoding 'utf8' -Raw
}

function Run-Vcpkg {
    Param(
        [Parameter(Mandatory = $false)]
        [Switch]$ForceExe,

        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    Run-VcpkgAndCaptureOutput -ForceExe:$ForceExe @TestArgs | Out-Null
}

Refresh-TestRoot
