. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $TestingRoot 'registries'
New-Item -ItemType Directory -Force $env:X_VCPKG_REGISTRIES_CACHE | Out-Null

function New-TestRegistry {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    New-Item -Path $Path -ItemType Directory -Force | Out-Null
    $Path = (Get-Item $Path).FullName

    git -C $Path @gitConfigOptions init . | Out-Null
    Throw-IfFailed

    return [pscustomobject]@{
        Path = $Path
        Current = @{}
        Versions = @{}
    }
}

function Set-TestRegistryPort {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$RegistryPath,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Version,
        [string[]]$Dependencies = @()
    )

    $portDir = Join-Path $RegistryPath "ports/$Name"
    New-Item -Path $portDir -ItemType Directory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $portDir 'portfile.cmake') `
        -Value 'set(VCPKG_POLICY_EMPTY_PACKAGE enabled)' `
        -Encoding Ascii

    $manifest = [ordered]@{
        name = $Name
        version = $Version
    }

    if ($Dependencies.Count -ne 0) {
        $manifest.dependencies = $Dependencies
    }

    Set-Content -LiteralPath (Join-Path $portDir 'vcpkg.json') `
        -Value (ConvertTo-Json -Depth 10 -InputObject $manifest) `
        -Encoding Ascii `
        -NoNewline
}

function Add-TestRegistryCommit {
    Param(
        [Parameter(Mandatory = $true)]
        $Registry,
        [Parameter(Mandatory = $true)]
        [hashtable[]]$Ports,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    foreach ($port in $Ports) {
        $previousVersion = $null
        if ($Registry.Current.ContainsKey($port.Name)) {
            $previousVersion = $Registry.Current[$port.Name]
        }

        if ($previousVersion -ne $port.Version) {
            if ($port.ContainsKey('Dependencies')) {
                Set-TestRegistryPort -RegistryPath $Registry.Path -Name $port.Name -Version $port.Version -Dependencies $port.Dependencies
            } else {
                Set-TestRegistryPort -RegistryPath $Registry.Path -Name $port.Name -Version $port.Version
            }
        }

        $Registry.Current[$port.Name] = $port.Version
    }

    git -C $Registry.Path @gitConfigOptions add ports | Out-Null
    Throw-IfFailed
    git -C $Registry.Path @gitConfigOptions commit --allow-empty -m $Message | Out-Null
    Throw-IfFailed

    foreach ($port in $Ports) {
        if (@($Registry.Versions[$port.Name])[0].version -eq $port.Version) {
            continue
        }

        $gitTree = git -C $Registry.Path rev-parse "HEAD:ports/$($port.Name)"
        Throw-IfFailed

        $versionEntry = [ordered]@{
            'git-tree' = $gitTree
            version = $port.Version
        }

        $previousVersions = @()
        if ($Registry.Versions.ContainsKey($port.Name)) {
            $previousVersions = @($Registry.Versions[$port.Name])
        }

        $Registry.Versions[$port.Name] = @($versionEntry) + $previousVersions
    }

    $baseline = [ordered]@{ default = [ordered]@{} }
    foreach ($portName in ($Registry.Current.Keys | Sort-Object)) {
        $baseline.default[$portName] = [ordered]@{ baseline = $Registry.Current[$portName] }
    }

    New-Item -Path (Join-Path $Registry.Path 'versions') -ItemType Directory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Registry.Path 'versions/baseline.json') `
        -Value (ConvertTo-Json -Depth 10 -InputObject $baseline) `
        -Encoding Ascii `
        -NoNewline

    foreach ($portName in $Registry.Versions.Keys) {
        $versionDir = Join-Path $Registry.Path "versions/$($portName[0])-"
        New-Item -Path $versionDir -ItemType Directory -Force | Out-Null
        $versionFile = [ordered]@{ versions = @($Registry.Versions[$portName]) }
        Set-Content -LiteralPath (Join-Path $versionDir "$portName.json") `
            -Value (ConvertTo-Json -Depth 10 -InputObject $versionFile) `
            -Encoding Ascii `
            -NoNewline
    }

    git -C $Registry.Path @gitConfigOptions add -A | Out-Null
    Throw-IfFailed
    git -C $Registry.Path @gitConfigOptions commit --amend --no-edit --allow-empty | Out-Null
    Throw-IfFailed

    $baselineCommit = git -C $Registry.Path rev-parse HEAD
    Throw-IfFailed
    return $baselineCommit
}

function New-TestManifest {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string[]]$Dependencies,
        [Parameter(Mandatory = $true)]
        [hashtable]$Configuration
    )

    New-Item -Path $Path -ItemType Directory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Path 'vcpkg.json') `
        -Value (ConvertTo-Json -Depth 10 -InputObject ([ordered]@{
            name = 'update-baseline-test'
            version = '1.0.0'
            dependencies = $Dependencies
        })) `
        -Encoding Ascii `
        -NoNewline
    Set-Content -LiteralPath (Join-Path $Path 'vcpkg-configuration.json') `
        -Value (ConvertTo-Json -Depth 10 -InputObject $Configuration) `
        -Encoding Ascii `
        -NoNewline
}

function Invoke-UpdateBaselineDryRun {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$ManifestRoot,
        [switch]$Quiet
    )

    $args = @('x-update-baseline') + $commonArgs + @('--dry-run', "--x-manifest-root=$ManifestRoot")
    if ($Quiet) {
        $args += '--quiet'
    }

    $out = Run-VcpkgAndCaptureOutput @args
    Throw-IfFailed
    return $out
}

function New-GitRegistryConfiguration {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$Repository,
        [Parameter(Mandatory = $true)]
        [string]$Baseline,
        [Parameter(Mandatory = $true)]
        [string[]]$Packages
    )

    return [ordered]@{
        kind = 'git'
        repository = $Repository
        baseline = $Baseline
        packages = $Packages
    }
}

function New-DefaultGitRegistryConfiguration {
    Param(
        [Parameter(Mandatory = $true)]
        [string]$Repository,
        [Parameter(Mandatory = $true)]
        [string]$Baseline
    )

    return [ordered]@{
        kind = 'git'
        repository = $Repository
        baseline = $Baseline
    }
}

Write-Trace 'build update-baseline registries'
$primaryRegistry = New-TestRegistry (Join-Path $TestingRoot 'primary-registry')
$primaryOldBaseline = Add-TestRegistryCommit -Registry $primaryRegistry -Message 'primary 1.0.0' -Ports @(
    @{ Name = 'direct-port'; Version = '1.0.0'; Dependencies = @('transitive-port') },
    @{ Name = 'transitive-port'; Version = '1.0.0' }
)
$primaryNewBaseline = Add-TestRegistryCommit -Registry $primaryRegistry -Message 'primary 2.0.0' -Ports @(
    @{ Name = 'direct-port'; Version = '2.0.0'; Dependencies = @('transitive-port') },
    @{ Name = 'transitive-port'; Version = '1.1.0' }
)

$unchangedSharedRegistry = New-TestRegistry (Join-Path $TestingRoot 'unchanged-shared-registry')
$unchangedSharedOldBaseline = Add-TestRegistryCommit -Registry $unchangedSharedRegistry -Message 'shared unchanged 1.0.0' -Ports @(
    @{ Name = 'shared-port'; Version = '1.0.0' }
)
$unchangedSharedNewBaseline = Add-TestRegistryCommit -Registry $unchangedSharedRegistry -Message 'shared unchanged still 1.0.0' -Ports @(
    @{ Name = 'shared-port'; Version = '1.0.0' }
)

$updatedSharedRegistry = New-TestRegistry (Join-Path $TestingRoot 'updated-shared-registry')
$updatedSharedOldBaseline = Add-TestRegistryCommit -Registry $updatedSharedRegistry -Message 'shared updated 1.0.0' -Ports @(
    @{ Name = 'shared-port'; Version = '1.0.0' }
)
$updatedSharedNewBaseline = Add-TestRegistryCommit -Registry $updatedSharedRegistry -Message 'shared updated 2.0.0' -Ports @(
    @{ Name = 'shared-port'; Version = '2.0.0' }
)

Write-Trace 'test direct and transitive dependency diff output'
$manifestDir = Join-Path $TestingRoot 'direct-and-transitive'
New-TestManifest -Path $manifestDir -Dependencies @('direct-port') -Configuration ([ordered]@{
    'default-registry' = New-DefaultGitRegistryConfiguration -Repository $primaryRegistry.Path -Baseline $primaryOldBaseline
})

$out = Invoke-UpdateBaselineDryRun -ManifestRoot $manifestDir
Throw-IfNonContains -Actual $out -Expected @"
Updating baselines has resulted in the following version updates:

Direct dependencies:
direct-port: 1.0.0 -> 2.0.0

Transitive dependencies:
transitive-port: 1.0.0 -> 1.1.0
"@

Write-Trace 'test quiet suppresses dependency diff output'
$out = Invoke-UpdateBaselineDryRun -ManifestRoot $manifestDir -Quiet
Throw-IfContains -Actual $out -Expected 'Updating baselines has resulted in the following version updates:'
Throw-IfContains -Actual $out -Expected 'direct-port: 1.0.0 -> 2.0.0'
Throw-IfContains -Actual $out -Expected 'transitive-port: 1.0.0 -> 1.1.0'

Write-Trace 'test no dependency diff output when baseline is already current'
$manifestDir = Join-Path $TestingRoot 'no-port-changes'
New-TestManifest -Path $manifestDir -Dependencies @('direct-port') -Configuration ([ordered]@{
    'default-registry' = New-DefaultGitRegistryConfiguration -Repository $primaryRegistry.Path -Baseline $primaryNewBaseline
})

$out = Invoke-UpdateBaselineDryRun -ManifestRoot $manifestDir
Throw-IfNonContains -Actual $out -Expected 'There were no changes in the ports.'
Throw-IfContains -Actual $out -Expected 'direct-port: 1.0.0 -> 2.0.0'

Write-Trace 'test shared port selected from updated registry is included in diff'
$manifestDir = Join-Path $TestingRoot 'shared-port-updated-registry-selected'
New-TestManifest -Path $manifestDir -Dependencies @('shared-port') -Configuration ([ordered]@{
    'default-registry' = New-DefaultGitRegistryConfiguration -Repository $unchangedSharedRegistry.Path -Baseline $unchangedSharedOldBaseline
    registries = @(
        (New-GitRegistryConfiguration -Repository $updatedSharedRegistry.Path -Baseline $updatedSharedOldBaseline -Packages @('shared-port'))
    )
})

$out = Invoke-UpdateBaselineDryRun -ManifestRoot $manifestDir
Throw-IfNonContains -Actual $out -Expected 'shared-port: 1.0.0 -> 2.0.0'

Write-Trace 'test shared port selected from unchanged registry is excluded from diff'
$manifestDir = Join-Path $TestingRoot 'shared-port-unchanged-registry-selected'
New-TestManifest -Path $manifestDir -Dependencies @('shared-port') -Configuration ([ordered]@{
    'default-registry' = New-DefaultGitRegistryConfiguration -Repository $updatedSharedRegistry.Path -Baseline $updatedSharedOldBaseline
    registries = @(
        (New-GitRegistryConfiguration -Repository $unchangedSharedRegistry.Path -Baseline $unchangedSharedOldBaseline -Packages @('shared-port'))
    )
})

$out = Invoke-UpdateBaselineDryRun -ManifestRoot $manifestDir
Throw-IfContains -Actual $out -Expected 'shared-port: 1.0.0 -> 2.0.0'
Throw-IfNonContains -Actual $out -Expected 'There were no changes in the ports.'
