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
            'location' = (Get-Item "$PSScriptRoot/../e2e_artifacts_registry").FullName;
        })
    } | ConvertTo-JSON | Out-File -Encoding ascii 'vcpkg-configuration.json' | Out-Null
}

function Test-Activations {
    Param(
        [switch]$One,
        [switch]$Two,
        [switch]$Three
    )

    if ($One -And $env:VCPKG_TEST_ARTIFACT_1_ACTIVATED -ne 'YES') {
        throw 'Expected vcpkg-test-artifact-1 to be activated'
    } elseif(-Not $One -And (Test-Path env:VCPKG_TEST_ARTIFACT_1_ACTIVATED)) {
        throw 'Expected vcpkg-test-artifact-1 to be deactivated'
    }

    if ($Two -And $env:VCPKG_TEST_ARTIFACT_2_ACTIVATED -ne 'YES') {
        throw 'Expected vcpkg-test-artifact-2 to be activated'
    } elseif(-Not $Two -And (Test-Path env:VCPKG_TEST_ARTIFACT_2_ACTIVATED)) {
        throw 'Expected vcpkg-test-artifact-2 to be deactivated'
    }

    if ($Three -And $env:VCPKG_TEST_ARTIFACT_3_ACTIVATED -ne 'YES') {
        throw 'Expected vcpkg-test-artifact-3 to be activated'
    } elseif(-Not $Three -And (Test-Path env:VCPKG_TEST_ARTIFACT_3_ACTIVATED)) {
        throw 'Expected vcpkg-test-artifact-3 to be deactivated'
    }
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
    Run-Vcpkg deactivate
    Throw-IfFailed
    Test-Activations
    Run-Vcpkg new --application
    Throw-IfFailed

    # deactivated-- no effects, issue warning -->deactivated
    $output = Run-VcpkgAndCaptureOutput deactivate
    Throw-IfFailed
    Test-DeactivationWarning $output

    # deactivated-- activate -->activated
    Reset-VcpkgConfiguration
    Run-Vcpkg add artifact artifacts-test:vcpkg-test-artifact-1
    Throw-IfFailed
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-Activations -One

    # environment_changed-- deactivate -->deactivated
    #   activated -> deactivated
    $output = Run-VcpkgAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output $ProjectRegex

    # deactivated-- use -->used
    $output = Run-VcpkgAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One
    # used-- use, stacks -->used
    $output = Run-VcpkgAndCaptureOutput use vcpkg-test-artifact-2
    # Note that we just remember what the user said, we don't try to resolve what it means
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1 \+ vcpkg-test-artifact-2"
    Test-Activations -One -Two

    # environment_changed-- deactivate -->deactivated
    #   used -> deactivated
    $output = Run-VcpkgAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output "artifacts-test:vcpkg-test-artifact-1 \+ vcpkg-test-artifact-2"

    # activated-- activate, deactivates first -->activated
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfFailed
    Test-Match $output "Activating: $ProjectRegex"
    Test-Activations -One
    Reset-VcpkgConfiguration
    Run-Vcpkg add artifact artifacts-test:vcpkg-test-artifact-3
    Throw-IfFailed
    Test-Activations -One
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: $ProjectRegex"
    Test-Match $output "Activating: $ProjectRegex"

    # activated-- use -->activate_use_stacked
    $output = Run-VcpkgAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One -Three

    # activate_use_stacked-- use, stacks -->activate_use_stacked
    $output = Run-VcpkgAndCaptureOutput use artifacts-test:vcpkg-test-artifact-2
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1 \+ artifacts-test:vcpkg-test-artifact-2"
    Test-Activations -One -Two -Three

    # activate_use_stacked-- activate, deactivates first -->activated
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1 \+ artifacts-test:vcpkg-test-artifact-2"
    Test-Match $output "Activating: $ProjectRegex"

    # environment_changed-- deactivate -->deactivated
    #   activated_stacked -> deactivated
    $output = Run-VcpkgAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: $ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One -Three
    $output = Run-VcpkgAndCaptureOutput deactivate
    Throw-IfFailed
    Test-NoDeactivationWarning $output "$ProjectRegex \+ artifacts-test:vcpkg-test-artifact-1"

    # used-- activate, deactivates first-->activated
    $output = Run-VcpkgAndCaptureOutput use artifacts-test:vcpkg-test-artifact-1
    Test-Match $output "Activating: artifacts-test:vcpkg-test-artifact-1"
    Test-Activations -One
    $output = Run-VcpkgAndCaptureOutput activate
    Throw-IfFailed
    Test-Activations -Three
    Test-Match $output "Deactivating: artifacts-test:vcpkg-test-artifact-1"
    Test-Match $output "Activating: $ProjectRegex"
} finally {
    Run-Vcpkg deactivate
    Pop-Location
}
