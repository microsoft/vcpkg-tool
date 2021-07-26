. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$builtinRegistryArgs = $commonArgs

Run-Vcpkg install @builtinRegistryArgs 'vcpkg-internal-e2e-test-port'
Throw-IfNotFailed

# We should not look into the versions directory unless we have a baseline,
# even if we pass the registries feature flag
Run-Vcpkg install @builtinRegistryArgs --feature-flags=registries 'vcpkg-internal-e2e-test-port'
Throw-IfNotFailed

Run-Vcpkg install @builtinRegistryArgs --feature-flags=registries 'zlib'
Throw-IfFailed

Write-Trace "Test git and filesystem registries"
Refresh-TestRoot
$filesystemRegistry = "$TestingRoot/filesystem-registry"
$gitRegistryUpstream = "$TestingRoot/git-registry-upstream"

# build a filesystem registry
Write-Trace "build a filesystem registry"
New-Item -Path $filesystemRegistry -ItemType Directory
$filesystemRegistry = (Get-Item $filesystemRegistry).FullName

Copy-Item -Recurse `
    -LiteralPath "$PSScriptRoot/../e2e_ports/vcpkg-internal-e2e-test-port" `
    -Destination "$filesystemRegistry"
New-Item `
    -Path "$filesystemRegistry/versions" `
    -ItemType Directory
Copy-Item `
    -LiteralPath "$PSScriptRoot/../e2e_ports/versions/baseline.json" `
    -Destination "$filesystemRegistry/versions/baseline.json"
New-Item `
    -Path "$filesystemRegistry/versions/v-" `
    -ItemType Directory

$vcpkgInternalE2eTestPortJson = @{
    "versions" = @(
        @{
            "version-string" = "1.0.0";
            "path" = "$/vcpkg-internal-e2e-test-port"
        }
    )
}
New-Item `
    -Path "$filesystemRegistry/versions/v-/vcpkg-internal-e2e-test-port.json" `
    -ItemType File `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgInternalE2eTestPortJson)


# build a git registry
Write-Trace "build a git registry"
New-Item -Path $gitRegistryUpstream -ItemType Directory
$gitRegistryUpstream = (Get-Item $gitRegistryUpstream).FullName

Push-Location $gitRegistryUpstream
try
{
    $gitConfigOptions = @(
        '-c', 'user.name=Nobody',
        '-c', 'user.email=nobody@example.com',
        '-c', 'core.autocrlf=false'
    )

    $CurrentTest = 'git init .'
    git @gitConfigOptions init .
    Throw-IfFailed
    Copy-Item -Recurse -LiteralPath "$PSScriptRoot/../e2e_ports/vcpkg-internal-e2e-test-port" -Destination .
    New-Item -Path './vcpkg-internal-e2e-test-port/foobar' -Value 'this is just to get a distinct git tree'

    $CurrentTest = 'git add -A'
    git @gitConfigOptions add -A
    Throw-IfFailed
    $CurrentTest = 'git commit'
    git @gitConfigOptions commit -m 'initial commit'
    Throw-IfFailed

    $vcpkgInternalE2eTestPortGitTree = git rev-parse 'HEAD:vcpkg-internal-e2e-test-port'
    $vcpkgInternalE2eTestPortVersionsJson = @{
        "versions" = @(
            @{
                "version-string" = "1.0.0";
                "git-tree" = $vcpkgInternalE2eTestPortGitTree
            }
        )
    }
    $vcpkgBaseline = @{
        "default" = @{
            "vcpkg-internal-e2e-test-port" = @{
                "baseline" = "1.0.0"
            }
        }
    }

    New-Item -Path './versions' -ItemType Directory
    New-Item -Path './versions/v-' -ItemType Directory

    New-Item -Path './versions/baseline.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgBaseline)
    New-Item -Path './versions/v-/vcpkg-internal-e2e-test-port.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgInternalE2eTestPortVersionsJson)

    $CurrentTest = 'git add -A'
    git @gitConfigOptions add -A
    Throw-IfFailed
    $CurrentTest = 'git commit'
    git @gitConfigOptions commit --amend --no-edit
    Throw-IfFailed

    $gitBaselineCommit = git rev-parse HEAD
    $gitRefVersionsObject = git rev-parse HEAD:versions
}
finally
{
    Pop-Location
}

# actually test the registries
Write-Trace "actually test the registries"
$vcpkgJson = @{
    "name" = "manifest-test";
    "version-string" = "1.0.0";
    "dependencies" = @(
        "vcpkg-internal-e2e-test-port"
    );
    # Use versioning features without a builtin-baseline
    "overrides" = @(@{
        "name" = "unused";
        "version" = "0";
    })
}

# test the filesystem registry
Write-Trace "test the filesystem registry"
$manifestDir = "$TestingRoot/filesystem-registry-test-manifest-dir"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "filesystem";
                "path" = $filesystemRegistry;
                "packages" = @( "vcpkg-internal-e2e-test-port" )
            }
        )
    }
    New-Item -Path 'vcpkg-configuration.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Run-Vcpkg install @builtinRegistryArgs '--feature-flags=registries,manifests'
    Throw-IfFailed
}
finally
{
    Pop-Location
}

# test the git registry
Write-Trace "test the git registry"
$manifestDir = "$TestingRoot/git-registry-test-manifest-dir"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "git";
                "repository" = $gitRegistryUpstream;
                "baseline" = $gitBaselineCommit;
                "packages" = @( "vcpkg-internal-e2e-test-port" )
            }
        )
    }
    New-Item -Path 'vcpkg-configuration.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Run-Vcpkg install @builtinRegistryArgs '--feature-flags=registries,manifests' --dry-run
    Throw-IfFailed
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$vcpkgInternalE2eTestPortGitTree
    # This is both the selected baseline as well as the current HEAD
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$gitRefVersionsObject
    # Dry run does not create a lockfile
    Require-FileNotExists $installRoot/vcpkg/vcpkg-lock.json

    Run-Vcpkg install @builtinRegistryArgs '--feature-flags=registries,manifests'
    Throw-IfFailed
    Require-FileEquals $installRoot/vcpkg/vcpkg-lock.json "{`n  $(ConvertTo-Json $gitRegistryUpstream): `"$gitBaselineCommit`"`n}`n"

    # Using the lock file means we can reinstall without pulling from the upstream registry
    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "git";
                "repository" = "/"; # An invalid repository
                "baseline" = $gitBaselineCommit;
                "packages" = @( "vcpkg-internal-e2e-test-port" )
            }
        )
    }

    Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
    Require-FileNotExists $installRoot
    New-Item -Path $installRoot/vcpkg -ItemType Directory
    # We pre-seed the install root with a lockfile for the invalid repository, so it isn't actually fetched from
    New-Item -Path $installRoot/vcpkg/vcpkg-lock.json -ItemType File `
        -Value "{`n  `"/`": `"$gitBaselineCommit`"`n}`n"
    Run-Vcpkg install @builtinRegistryArgs '--feature-flags=registries,manifests'
    Throw-IfFailed
}
finally
{
    Pop-Location
}
