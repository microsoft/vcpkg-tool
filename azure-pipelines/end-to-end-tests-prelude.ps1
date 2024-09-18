$TestingRoot = Join-Path $WorkingRoot 'testing'
$buildtreesRoot = Join-Path $TestingRoot 'buildtrees'
$installRoot = Join-Path $TestingRoot 'installed'
$packagesRoot = Join-Path $TestingRoot 'packages'
$NuGetRoot = Join-Path $TestingRoot 'nuget'
$NuGetRoot2 = Join-Path $TestingRoot 'nuget2'
$ArchiveRoot = Join-Path $TestingRoot 'archives'
$VersionFilesRoot = Join-Path $TestingRoot 'version-test'
$DownloadsRoot = Join-Path $TestingRoot 'downloads'
$AssetCache = Join-Path $TestingRoot 'asset-cache'

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

$unusedStdoutFile = Join-Path $WorkingRoot 'unused-stdout.txt'
$stderrFile = Join-Path $WorkingRoot 'last-stderr.txt'

$Script:CurrentTest = 'unassigned'

function Refresh-TestRoot {
    Remove-Item -Recurse -Force $TestingRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $TestingRoot | Out-Null
    New-Item -ItemType Directory -Force $NuGetRoot | Out-Null
    New-Item -ItemType Directory -Force $DownloadsRoot | Out-Null
    New-Item -ItemType Directory -Force $AssetCache | Out-Null
}

function Refresh-Downloads{
    Remove-Item -Recurse -Force $DownloadsRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $DownloadsRoot | Out-Null
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
    & "$thisVcpkg" @testArgs 1> $unusedStdoutFile 2> $stderrFile
    $result = Get-Content -LiteralPath $stderrFile -Encoding 'utf8' -Raw
    if ($null -eq $result) {
        $result = [string]::Empty
    }
    return $result
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

# https://github.com/actions/toolkit/blob/main/docs/commands.md#problem-matchers
# .github/workflows/matchers.json
function Remove-Problem-Matchers {
    Write-Host "::remove-matcher owner=vcpkg-msvc::"
    Write-Host "::remove-matcher owner=vcpkg-gcc::"
    Write-Host "::remove-matcher owner=vcpkg-catch::"
}

function Restore-Problem-Matchers {
    Write-Host "::add-matcher::.github/workflows/matchers.json"
}

function Set-EmptyTestPort {
    Param(
        [Parameter(Mandatory)][ValidateNotNullOrWhitespace()]
        [string]$Name,
        [Parameter(Mandatory)][ValidateNotNullOrWhitespace()]
        [string]$Version,
        [Parameter(Mandatory)][ValidateNotNullOrWhitespace()]
        [string]$PortsRoot,
        [switch]$Malformed
    )

    $portDir = Join-Path $PortsRoot $Name

    New-Item -ItemType Directory -Force -Path $portDir | Out-Null
    Set-Content -Value "set(VCPKG_POLICY_EMPTY_PACKAGE enabled)" -LiteralPath (Join-Path $portDir 'portfile.cmake') -Encoding Ascii

    if ($Malformed) {
        # Add bad trailing comma
        $json = @"
{
  "name": "$Name",
  "version": "$Version",
}
"@
    } else {
        $json = @"
{
  "name": "$Name",
  "version": "$Version"
}
"@
    }
    Set-Content -Value $json -LiteralPath (Join-Path $portDir 'vcpkg.json') -Encoding Ascii
}

Refresh-TestRoot
