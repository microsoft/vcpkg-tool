$TestingRoot = Join-Path $WorkingRoot 'testing'
$buildtreesRoot = Join-Path $TestingRoot 'buildtrees'
$installRoot = Join-Path $TestingRoot 'installed'
$packagesRoot = Join-Path $TestingRoot 'packages'
$NuGetRoot = Join-Path $TestingRoot 'nuget'
$NuGetRoot2 = Join-Path $TestingRoot 'nuget2'
$ArchiveRoot = Join-Path $TestingRoot 'archives'
$VersionFilesRoot = Join-Path $TestingRoot 'version-test'
$TestDownloadsRoot = Join-Path $TestingRoot 'downloads'
$AssetCache = Join-Path $TestingRoot 'asset-cache'

$directoryArgs = @(
    "--x-buildtrees-root=$buildtreesRoot",
    "--x-install-root=$installRoot",
    "--x-packages-root=$packagesRoot"
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

if (Test-Path env:VCPKG_DOWNLOADS) {
    $DefaultDownloadsRoot = $env:VCPKG_DOWNLOADS
} else {
    $DefaultDownloadsRoot = Join-Path $VcpkgRoot 'downloads'
}

$unusedStdoutFile = Join-Path $WorkingRoot 'unused-stdout.txt'
$stderrFile = Join-Path $WorkingRoot 'last-stderr.txt'

$Script:CurrentTest = 'unassigned'

function Refresh-TestRoot {
    Remove-Item -Recurse -Force $TestingRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $TestingRoot | Out-Null
    New-Item -ItemType Directory -Force $NuGetRoot | Out-Null
    New-Item -ItemType Directory -Force $TestDownloadsRoot | Out-Null
    New-Item -ItemType Directory -Force $AssetCache | Out-Null
}

function Refresh-TestDownloads {
    Remove-Item -Recurse -Force $TestDownloadsRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $TestDownloadsRoot | Out-Null
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
    $global:LASTEXITCODE = 0
}

function Write-Trace ([string]$text) {
    Write-Host (@($MyInvocation.ScriptName, ":", $MyInvocation.ScriptLineNumber, ": ", $text) -join "")
}

function Run-VcpkgImplAndCaptureOutput {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$ThisVcpkg,
        [Parameter(Mandatory = $true)]
        [string[]]$TestArgs
    )
    $Script:CurrentTest = "$ThisVcpkg $($testArgs -join ' ')"
    Write-Host -ForegroundColor red $Script:CurrentTest
    $result = (& "$ThisVcpkg" @testArgs) | Out-String
    Write-Host -ForegroundColor Gray $result
    $result.Replace("`r`n", "`n")
}

function Run-VcpkgShellAndCaptureOutput {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )

    return Run-VcpkgImplAndCaptureOutput -ThisVcpkg $VcpkgPs1 -TestArgs $TestArgs
}

function Run-VcpkgAndCaptureOutput {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )

    return Run-VcpkgImplAndCaptureOutput -ThisVcpkg $VcpkgExe -TestArgs $TestArgs
}

function Run-VcpkgAndCaptureStdErr {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )

    $Script:CurrentTest = "$VcpkgExe $($testArgs -join ' ')"
    Write-Host -ForegroundColor red $Script:CurrentTest
    & "$VcpkgExe" @testArgs 1> $unusedStdoutFile 2> $stderrFile
    $result = Get-Content -LiteralPath $stderrFile -Encoding 'utf8' -Raw
    if ($null -eq $result) {
        $result = [string]::Empty
    }
    return $result.Replace("`r`n", "`n")
}

function Run-VcpkgShell {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    Run-VcpkgShellAndCaptureOutput @TestArgs | Out-Null
}

function Run-Vcpkg {
    Param(
        [Parameter(ValueFromRemainingArguments)]
        [string[]]$TestArgs
    )
    Run-VcpkgAndCaptureOutput @TestArgs | Out-Null
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
        [string]$PortVersion,
        [Parameter(Mandatory)][ValidateNotNullOrWhitespace()]
        [string]$PortsRoot,
        [switch]$Malformed
    )
    Write-Host "Setting $Name to $Version#$PortVersion @ $PortsRoot"

    $portDir = Join-Path $PortsRoot $Name

    New-Item -ItemType Directory -Force -Path $portDir | Out-Null
    Set-Content -Value "set(VCPKG_POLICY_EMPTY_PACKAGE enabled)" -LiteralPath (Join-Path $portDir 'portfile.cmake') -Encoding Ascii

    $json = @"
{
  "name": "$Name",
  "version": "$Version"
"@

    if (-not $null -eq $PortVersion)
    {
        $json += ",`n  `"port-version`": $PortVersion"
    }

    if ($Malformed) {
        $json += ','
    }

    $json += "`n}`n"

    Set-Content -Value $json -LiteralPath (Join-Path $portDir 'vcpkg.json') -Encoding Ascii -NoNewline
    git -C $PortsRoot status
}

function Throw-IfNonEqual {
    Param(
        [string]$Actual,
        [string]$Expected
    )
    if ($Actual -ne $Expected) {
        Set-Content -Value $Expected -LiteralPath "$TestingRoot/expected.txt"
        Set-Content -Value $Actual -LiteralPath "$TestingRoot/actual.txt"
        git diff --no-index -- "$TestingRoot/expected.txt" "$TestingRoot/actual.txt"
        Write-Stack
        throw "Expected '$Expected' but got '$Actual'"
    }
}

function Throw-IfNonEndsWith {
    Param(
        [string]$Actual,
        [string]$Expected
    )

    [string]$actualSuffix = $actual
    $actualLength = $Actual.Length
    if ($actualLength -gt $expected.Length) {
        $actualSuffix = $Actual.Substring($actualLength - $expected.Length, $expected.Length)
    }

    if ($actualSuffix -ne $Expected) {
        Set-Content -Value $Expected -LiteralPath "$TestingRoot/expected.txt"
        Set-Content -Value $Actual -LiteralPath "$TestingRoot/actual.txt"
        git diff --no-index -- "$TestingRoot/expected.txt" "$TestingRoot/actual.txt"
        Write-Stack
        throw "Expected '$Expected' but got '$actualSuffix'"
    }
}

function Throw-IfContains {
    Param(
        [string]$Actual,
        [string]$Expected
    )
    if ($Actual.Contains($Expected)) {
        Set-Content -Value $Expected -LiteralPath "$TestingRoot/expected.txt"
        Set-Content -Value $Actual -LiteralPath "$TestingRoot/actual.txt"
        git diff --no-index -- "$TestingRoot/expected.txt" "$TestingRoot/actual.txt"
        Write-Stack
        throw "Expected '$Expected' to not be in '$Actual'"
    }
}

function Throw-IfNonContains {
    Param(
        [string]$Actual,
        [string]$Expected
    )
    if (-not ($Actual.Contains($Expected))) {
        Set-Content -Value $Expected -LiteralPath "$TestingRoot/expected.txt"
        Set-Content -Value $Actual -LiteralPath "$TestingRoot/actual.txt"
        git diff --no-index -- "$TestingRoot/expected.txt" "$TestingRoot/actual.txt"
        Write-Stack
        throw "Expected '$Expected' to be in '$Actual'"
    }
}

function Test-ManifestInfo {
    param (
        [string]$ManifestInfoPath,
        [string]$VcpkgDir,
        [string]$ManifestRoot
    )

    if (-not (Test-Path $ManifestInfoPath)) {
        Throw "manifest-info.json missing from $VcpkgDir"
    }

    $manifestInfoContent = Get-Content $ManifestInfoPath -Raw | ConvertFrom-Json

    if ($manifestInfoContent.'manifest-path' -ne (Join-Path -Path $ManifestRoot -ChildPath "vcpkg.json")) {
        Throw "Mismatch in manifest-path. Expected: $ManifestRoot, Found: $($manifestInfoContent.'manifest-path')"
    }
}

Refresh-TestRoot
