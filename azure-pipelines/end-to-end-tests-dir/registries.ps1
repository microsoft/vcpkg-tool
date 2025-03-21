. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $TestingRoot 'registries'
New-Item -ItemType Directory -Force $env:X_VCPKG_REGISTRIES_CACHE | Out-Null

Run-Vcpkg install @commonArgs 'vcpkg-internal-e2e-test-port'
Throw-IfNotFailed

# We should not look into the versions directory unless we have a baseline,
# even if we pass the registries feature flag
Run-Vcpkg install @commonArgs --feature-flags=registries 'vcpkg-internal-e2e-test-port'
Throw-IfNotFailed

Run-Vcpkg install @commonArgs --feature-flags=registries 'vcpkg-cmake'
Throw-IfFailed

Write-Trace "Test git and filesystem registries"
Refresh-TestRoot
New-Item -ItemType Directory -Force $env:X_VCPKG_REGISTRIES_CACHE | Out-Null
$filesystemRegistry = "$TestingRoot/filesystem-registry"
$gitRegistryUpstream = "$TestingRoot/git-registry-upstream"

# build a filesystem registry
Write-Trace "build a filesystem registry"
New-Item -Path $filesystemRegistry -ItemType Directory
$filesystemRegistry = (Get-Item $filesystemRegistry).FullName

Copy-Item -Recurse `
    -LiteralPath "$PSScriptRoot/../e2e-ports/vcpkg-internal-e2e-test-port" `
    -Destination "$filesystemRegistry"
New-Item `
    -Path "$filesystemRegistry/versions" `
    -ItemType Directory
Set-Content -Value @"
{
    "default": {
        "vcpkg-internal-e2e-test-port": { "baseline": "1.0.0" }
    }
}
"@ -LiteralPath "$filesystemRegistry/versions/baseline.json"
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
    $gitMainBranch = 'main'
    $gitSecondaryBranch = 'secondary'

    $CurrentTest = 'git init .'
    git @gitConfigOptions init .
    Throw-IfFailed

    # Create git registry with vcpkg-internal-e2e-test-port in the main branch
    $CurrentTest = 'git switch --orphan'
    git @gitConfigOptions switch --orphan $gitMainBranch
    Throw-IfFailed

    Copy-Item -Recurse -LiteralPath "$PSScriptRoot/../e2e-ports/vcpkg-internal-e2e-test-port" -Destination .
    New-Item -Path './vcpkg-internal-e2e-test-port/foobar' -Value 'this is just to get a distinct git tree'

    $CurrentTest = 'git add -A'
    git @gitConfigOptions add -A
    Throw-IfFailed
    $CurrentTest = 'git commit -m'
    git @gitConfigOptions commit -m 'add vcpkg-internal-e2e-test-port'
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
    $CurrentTest = 'git commit --amend --no-edit'
    git @gitConfigOptions commit --amend --no-edit
    Throw-IfFailed

    $gitMainBaselineCommit = git rev-parse HEAD
    $gitMainRefVersionsObject = git rev-parse HEAD:versions

    # Create git registry with vcpkg-internal-e2e-test-port2 in the secondary branch

    $CurrentTest = 'git switch --orphan'
    git @gitConfigOptions switch --orphan $gitSecondaryBranch
    Throw-IfFailed

    Copy-Item -Recurse -LiteralPath "$PSScriptRoot/../e2e-ports/vcpkg-internal-e2e-test-port2" -Destination .
    New-Item -Path './vcpkg-internal-e2e-test-port2/foobaz' -Value 'this is just to get a distinct git tree'

    $CurrentTest = 'git add -A'
    git @gitConfigOptions add -A
    Throw-IfFailed
    $CurrentTest = 'git commit -m'
    git @gitConfigOptions commit -m 'add vcpkg-internal-e2e-test-port2'
    Throw-IfFailed

    $vcpkgInternalE2eTestPort2GitTree = git rev-parse 'HEAD:vcpkg-internal-e2e-test-port2'
    $vcpkgInternalE2eTestPortVersionsJson = @{
        "versions" = @(
            @{
                "version-string" = "1.0.0";
                "git-tree" = $vcpkgInternalE2eTestPort2GitTree
            }
        )
    }
    $vcpkgBaseline = @{
        "default" = @{
            "vcpkg-internal-e2e-test-port2" = @{
                "baseline" = "1.0.0"
            }
        }
    }

    New-Item -Path './versions' -ItemType Directory
    New-Item -Path './versions/v-' -ItemType Directory

    New-Item -Path './versions/baseline.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgBaseline)
    New-Item -Path './versions/v-/vcpkg-internal-e2e-test-port2.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgInternalE2eTestPortVersionsJson)

    $CurrentTest = 'git add -A'
    git @gitConfigOptions add -A
    Throw-IfFailed
    $CurrentTest = 'git commit --amend --no-edit'
    git @gitConfigOptions commit --amend --no-edit
    Throw-IfFailed

    $gitSecondaryBaselineCommit = git rev-parse HEAD
    $gitSecondaryRefVersionsObject = git rev-parse HEAD:versions

    $CurrentTest = 'git switch'
    git @gitConfigOptions switch $gitMainBranch
    Throw-IfFailed
}
finally
{
    Pop-Location
}

# actually test the registries
Write-Trace "actually test the registries"

# test the filesystem registry
Write-Trace "test the filesystem registry"
$manifestDir = "$TestingRoot/filesystem-registry-test-manifest-dir"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
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
    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)
    New-Item -Path 'vcpkg-configuration.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Run-Vcpkg install @commonArgs '--feature-flags=registries,manifests'
    Throw-IfFailed
}
finally
{
    Pop-Location
}

# test the filesystem registry with a relative path
Write-Trace "test the filesystem registry with a relative path"
$manifestDir = "$TestingRoot/filesystem-registry-test-manifest-dir"
Remove-Item -Recurse -Force $manifestDir -ErrorAction SilentlyContinue

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
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
    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "filesystem";
                "path" = "../filesystem-registry";
                "packages" = @( "vcpkg-internal-e2e-test-port" )
            }
        )
    }
    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)
    New-Item -Path 'vcpkg-configuration.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Run-Vcpkg install @commonArgs '--feature-flags=registries,manifests'
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
    $vcpkgJson = @{
        "name" = "manifest-test";
        "version-string" = "1.0.0";
        "dependencies" = @(
            "vcpkg-internal-e2e-test-port",
            "vcpkg-internal-e2e-test-port2"
        );
        # Use versioning features without a builtin-baseline
        "overrides" = @(@{
            "name" = "unused";
            "version" = "0";
        })
    }

    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "git";
                "repository" = $gitRegistryUpstream;
                "baseline" = $gitMainBaselineCommit;
                 "packages" = @( "vcpkg-internal-e2e-test-port" )
            },
            @{
                "kind" = "git";
                "repository" = $gitRegistryUpstream;
                "reference" = $gitSecondaryBranch;
                "baseline" = $gitSecondaryBaselineCommit;
                 "packages" = @( "vcpkg-internal-e2e-test-port2" )
            }
        )
    }

    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    New-Item -Path 'vcpkg-configuration.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Run-Vcpkg install @commonArgs '--feature-flags=registries,manifests' --dry-run
    Throw-IfFailed
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$vcpkgInternalE2eTestPortGitTree
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$vcpkgInternalE2eTestPort2GitTree
    # This is both the selected baseline as well as the current HEAD
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$gitMainRefVersionsObject
    Require-FileExists $env:X_VCPKG_REGISTRIES_CACHE/git-trees/$gitSecondaryRefVersionsObject
    # Dry run does not create a lockfile
    Require-FileNotExists $installRoot/vcpkg/vcpkg-lock.json

    Run-Vcpkg install @commonArgs '--feature-flags=registries,manifests'
    Throw-IfFailed

    $expectedVcpkgLockJson = "{$(ConvertTo-Json $gitRegistryUpstream):{
        `"HEAD`" : `"$gitMainBaselineCommit`",
        `"$gitSecondaryBranch`" : `"$gitSecondaryBaselineCommit`"
    }}"

    Require-JsonFileEquals $installRoot/vcpkg/vcpkg-lock.json $expectedVcpkgLockJson

    # Using the lock file means we can reinstall without pulling from the upstream registry
    $vcpkgConfigurationJson = @{
        "default-registry" = $null;
        "registries" = @(
            @{
                "kind" = "git";
                "repository" = "/"; # An invalid repository
                "baseline" = $gitMainBaselineCommit;
                 "packages" = @( "vcpkg-internal-e2e-test-port" )
            },
            @{
                "kind" = "git";
                "repository" = "/"; # An invalid repository
                "reference" = $gitSecondaryBranch;
                "baseline" = $gitSecondaryBaselineCommit;
                 "packages" = @( "vcpkg-internal-e2e-test-port2" )
            }
        )
    }

    Set-Content -Path 'vcpkg-configuration.json' `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)

    Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
    Require-FileNotExists $installRoot
    New-Item -Path $installRoot/vcpkg -ItemType Directory
    # We pre-seed the install root with a lockfile for the invalid repository, so it isn't actually fetched from
    $vcpkgLockJson = @{
        "/" = @{
            "HEAD" = $gitMainBaselineCommit;
            $gitSecondaryBranch = $gitSecondaryBaselineCommit
        }
    }
   New-Item -Path $installRoot/vcpkg/vcpkg-lock.json -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgLockJson)
    Run-Vcpkg install @commonArgs '--feature-flags=registries,manifests'
    Throw-IfFailed
}
finally
{
    Pop-Location
}

# test builtin registry
Write-Trace "test builtin registry with baseline"
$manifestDir = "$TestingRoot/manifest-builtin"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
    $vcpkgJson = @{
        "name" = "manifest-test";
        "version-string" = "1.0.0";
        "builtin-baseline" = "a4b5cde7f504c1bbbbc455f4a6ee60efd9034772";
    }

    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    Run-Vcpkg search @commonArgs zlib
    Throw-IfFailed
}
finally
{
    Pop-Location
}


# test nonexistent overrides regression (https://github.com/microsoft/vcpkg/issues/36994)
Write-Trace "test nonexistent overrides regression"
$manifestDir = "$TestingRoot/manifest-nonexistent-override"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
    $vcpkgJson = @{
        "name" = "manifest-test";
        "version" = "1.0.0";
        "dependencies" = @(
            "nonexistent"
        );
        "overrides" = @(@{
            "name" = "nonexistent";
            "version" = "0";
        });
        "vcpkg-configuration" = @{
            "default-registry" = @{
                "kind" = "git";
                "repository" = "https://github.com/microsoft/vcpkg";
                "baseline" = "a4b5cde7f504c1bbbbc455f4a6ee60efd9034772";
            };
        };
    }

    New-Item -Path 'vcpkg.json' -ItemType File `
        -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    $out = Run-VcpkgAndCaptureOutput install @commonArgs
    Throw-IfNotFailed
    if (-not $out.Contains("error: nonexistent does not exist")) {
        throw "Expected nonexistent package to be mentioned in the output"
    }
}
finally
{
    Pop-Location
}

# test x-update-baseline reference in registry
Write-Trace "test x-update-baseline reference in registry"
$manifestDir = "$TestingRoot/update-baseline-registry-reference"

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName

Push-Location $manifestDir
try
{
    $gitMainBranch = 'master'
    $gitSecondaryBranch = 'secondary'

    $CurrentTest = 'git init .'
    git @gitConfigOptions init . --initial-branch=$gitMainBranch
    Throw-IfFailed

    $vcpkgBaseline = @{
        "default" = @{
        }
    }

    New-Item -Path './ports' -ItemType Directory
    New-Item -Path './versions' -ItemType Directory

    New-Item -Path './versions/baseline.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgBaseline)

    $CurrentTest = 'git add versions/baseline.json'
    git @gitConfigOptions add versions/baseline.json
    Throw-IfFailed

    $CurrentTest = 'git commit initial commit'
    git @gitConfigOptions commit -m "initial commit"
    Throw-IfFailed

    $vcpkgConfigurationJson = @{
        "default-registry"= @{
            "kind" = "git";
            "baseline" = "";
            "repository" = $manifestDir;
        }
    }

    $vcpkgJson = @{}
    New-Item -Path './vcpkg-configuration.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson)
    New-Item -Path './vcpkg.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

    $CurrentTest = 'vcpkg x-update-baseline'
    $out = Run-VcpkgAndCaptureOutput x-update-baseline
    Throw-IfFailed

    $CurrentTest = 'git rev-parse HEAD'
    $registryBaseline = git rev-parse HEAD
    Throw-IfFailed

    $configurationBefore = Get-Content "$manifestDir/vcpkg-configuration.json" | ConvertFrom-Json -AsHashTable
    if ($configurationBefore["default-registry"]["baseline"] -ne $registryBaseline) {
        throw "x-update-baseline baseline mismatch"
    }

    $CurrentTest = 'git checkout secondary branch'
    git @gitConfigOptions checkout -b $gitSecondaryBranch
    Throw-IfFailed

    $CurrentTest = 'git commit empty commit'
    git @gitConfigOptions commit --allow-empty -m "empty commit"
    Throw-IfFailed

    $CurrentTest = 'git checkout main branch'
    git @gitConfigOptions checkout $gitMainBranch
    Throw-IfFailed

    $vcpkgConfigurationJson = @{
        "default-registry"= @{
            "kind" = "git";
            "baseline" = "";
            "repository" = $manifestDir;
            "reference" = "secondary";
        }
    }

    New-Item -Path './vcpkg-configuration.json' -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgConfigurationJson) -Force

    $CurrentTest = 'git rev-parse branch'
    $refBaseline = git rev-parse $gitSecondaryBranch
    Throw-IfFailed

    $CurrentTest = 'vcpkg x-update-baseline on reference'
    $out = Run-VcpkgAndCaptureOutput x-update-baseline
    Throw-IfFailed

    $configurationBefore = Get-Content "$manifestDir/vcpkg-configuration.json" | ConvertFrom-Json -AsHashTable

    if ($configurationBefore["default-registry"]["baseline"] -ne $refBaseline) {
        throw "Unexpected baseline mismatch on reference branch"
    }
}
finally
{
    Pop-Location
}
