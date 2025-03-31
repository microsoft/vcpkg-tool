. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

if (-Not $RunArtifactsTests) {
    return
}

# Testing interaction between use + activate + deactivate
# https://github.com/microsoft/vcpkg/issues/29978

function Reset-VcpkgConfiguration {
    @{
        registries = @(@{
            'name' = 'artifacts-test';
            'kind' = 'artifact';
            'location' = (Get-Item "$PSScriptRoot/../e2e-artifacts-registry").FullName;
        })
    } | ConvertTo-JSON | Out-File -Encoding ascii 'vcpkg-configuration.json' | Out-Null
}

function Test-Activation {
    Param(
        [Parameter(Mandatory=$true)]
        [int]$Number,
        [Parameter(Mandatory=$true)]
        [bool]$Expected
    )

    [string]$combined = [System.Environment]::GetEnvironmentVariable('VCPKG_TEST_ARTIFACTS_PATHS')
    if ($combined -eq $null) {
        $combined = ''
    }

    # This is technically depending on the implementation detail that the artifact name ends up in
    # in the resulting path; if this is a problem in the future the test artifacts could be changed
    # to install real content which would be disinguishable directly.
    [bool]$combinedActivated = $combined.Contains("vcpkg.test.artifact.$($Number)")
    $singleActivationName = "VCPKG_TEST_ARTIFACT_$($Number)_ACTIVATED"
    $single = [System.Environment]::GetEnvironmentVariable($singleActivationName)
    [bool]$singleActivated = $single -eq 'YES'

    if ($combinedActivated -ne $singleActivated) {
        throw "When testing activation of vcpkg-test-artifact-$($Number), the combined variable and single variable disagreed on the activation status`n" `
            + "VCPKG_TEST_ARTIFACTS_PATHS: $combined`n" `
            + "$($singleActivationName): $single`n";
    }

    if ($Expected -And -Not $combinedActivated) {
        throw "Expected vcpkg-test-artifact-$($Number) to be activated"
    } elseif(-Not $Expected -And $combinedActivated) {
        throw "Expected vcpkg-test-artifact-$($Number) to be deactivated"
    }
}

function Test-Activations {
    Param(
        [switch]$One,
        [switch]$Two,
        [switch]$Three
    )

    Test-Activation -Number 1 -Expected $One.ToBool()
    Test-Activation -Number 2 -Expected $Two.ToBool()
    Test-Activation -Number 3 -Expected $Three.ToBool()
}

function Test-Match {
    Param(
        [string]$Output,
        [string]$Regex
    )

    if (-Not ($Output -Match $Regex)) {
        throw "Expected output: $Regex"
    }
}

function Test-NoMatch {
    Param(
        [string]$Output,
        [string]$Regex
    )

    if ($Output -Match $Regex) {
        throw "Unxpected output: $Regex"
    }
}

function Test-DeactivationWarning {
    Param(
        [string]$Output
    )

    Test-Match $Output 'warning: nothing is activated, no changes have been made'
    Test-NoMatch $Output 'Deactivating:'
    Test-Activations
}

function Test-NoDeactivationWarning {
    Param(
        [string]$Output,
        [string]$StackMatch
    )

    Test-NoMatch $Output 'warning: nothing is activated, no changes have been made'
    Test-Match $Output "Deactivating: $StackMatch"
    Test-Activations
}

Refresh-TestRoot

$Project = Join-Path $TestingRoot 'artifacts-project'
$ProjectRegex = [System.Text.RegularExpressions.Regex]::Escape($Project)
New-Item -Path $Project -Type Directory -Force
Push-Location $Project
try {
    Run-VcpkgShell deactivate
    Throw-IfFailed
    Test-Activations
    Run-Vcpkg new --application
    Throw-IfFailed

    # deactivated-- no effects, issue warning -->deactivated
    $output = Run-VcpkgShellAndCaptureOutput deactivate
    Throw-IfFailed
    Test-DeactivationWarning $output

    # deactivated-- activate -->activated
    Reset-VcpkgConfiguration
    Run-Vcpkg add artifact artifacts-test:vcpkg-test-artifact-1
    Throw-IfFailed
    $output = Run-VcpkgShellAndCaptureOutput activate
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-Activations -One

    # environment_changed-- deactivate -->deactivated
    #   activated -> deactivated
    $output = Run-VcpkgShellAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output $ProjectRegex

    # deactivated-- use -->used
    $output = Run-VcpkgShellAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One
    # used-- use, stacks -->used
    $output = Run-VcpkgShellAndCaptureOutput use vcpkg-test-artifact-2
    # Note that we just remember what the user said, we don't try to resolve what it means
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1 \+ vcpkg-test-artifact-2"
    Test-Activations -One -Two

    # environment_changed-- deactivate -->deactivated
    #   used -> deactivated
    $output = Run-VcpkgShellAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output "artifacts-test:vcpkg-test-artifact-1 \+ vcpkg-test-artifact-2"

    # activated-- activate, deactivates first -->activated
    $output = Run-VcpkgShellAndCaptureOutput activate
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-Activations -One
    Reset-VcpkgConfiguration
    Run-Vcpkg add artifact artifacts-test:vcpkg-test-artifact-3
    Throw-IfFailed
    Test-Activations -One
    $output = Run-VcpkgShellAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: $ProjectRegex"
    Test-Match $output "Activating: $ProjectRegex"

    # activated-- use -->activate_use_stacked
    $output = Run-VcpkgShellAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One -Three

    # activate_use_stacked-- use, stacks -->activate_use_stacked
    $output = Run-VcpkgShellAndCaptureOutput use artifacts-test:vcpkg-test-artifact-2
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1 \+ artifacts-test:vcpkg-test-artifact-2"
    Test-Activations -One -Two -Three

    # activate_use_stacked-- activate, deactivates first -->activated
    $output = Run-VcpkgShellAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1 \+ artifacts-test:vcpkg-test-artifact-2"
    Test-Match $output "Activating: $ProjectRegex"

    # environment_changed-- deactivate -->deactivated
    #   activated_stacked -> deactivated
    $output = Run-VcpkgShellAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One -Three
    $output = Run-VcpkgShellAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output "$ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"

    # used-- activate, deactivates first-->activated
    $output = Run-VcpkgShellAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One
    $output = Run-VcpkgShellAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: artifacts-test:vcpkg-test-artifact-1"
    Test-Match $output "Activating: $ProjectRegex"

    # test "no postscript" warning:
    #  can't deactivate without postscript:
    $output = Run-VcpkgAndCaptureOutput deactivate
    Throw-IfNotFailed
    Test-Match $output "no postscript file: run vcpkg-shell with the same arguments"

    $output = Run-VcpkgShellAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output $ProjectRegex

    #  can't activate without the shell function:
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfNotFailed
    Test-Match $output "no postscript file: run vcpkg-shell with the same arguments"
    Test-Activations

    #  unless --json passed
    $output = Run-VcpkgAndCaptureOutput activate --json (Join-Path $Project 'result.json')
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-NoMatch $output "no postscript file: run vcpkg-shell with the same arguments"
    Test-Activations # no shell activation

    #  or --msbuild-props passed
    $output = Run-VcpkgAndCaptureOutput activate --msbuild-props (Join-Path $Project 'result.props')
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-NoMatch $output "no postscript file: run vcpkg-shell with the same arguments"
    Test-Activations # no shell activation
} finally {
    Run-Vcpkg deactivate
    Pop-Location
}

$output = Run-VcpkgAndCaptureOutput x-update-registry microsoft
Throw-IfFailed
Test-Match $output "Updating registry data from microsoft"

$output = Run-VcpkgAndCaptureOutput x-update-registry https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/main.zip
Throw-IfFailed
Test-Match $output "Updating registry data from microsoft"

$output = Run-VcpkgAndCaptureOutput x-update-registry https://example.com
Throw-IfNotFailed
Test-Match $output "\[https://example.com/\] could not be updated; it could be malformed\."
