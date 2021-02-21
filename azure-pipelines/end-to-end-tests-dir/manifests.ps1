. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"


Write-Trace "test manifest features"
$manifestDir = "$TestingRoot/manifest-dir"

$manifestDirArgs = $commonArgs + @("--x-manifest-root=$manifestDir")
$noDefaultFeatureArgs = $manifestDirArgs + @('--x-no-default-features')

function feature {
    @{
        'description' = '';
        'dependencies' = $args;
    }
}

$vcpkgJson = @{
    'name' = "manifest-test";
    'version' = "1.0.0";
    'default-features' = @( 'default-fail' );
    'features' = @{
        'default-fail' = feature 'vcpkg-fail-if-depended-upon';
        'copied-feature' = feature 'vcpkg-empty-port'
        'multiple-dep-1' = feature 'vcpkg-empty-port'
        'multiple-dep-2' = feature 'vcpkg-empty-port'
        'no-default-features-1' = feature @{
            'name' = 'vcpkg-default-features-fail';
            'default-features' = $False;
        };
        'no-default-features-2' = feature @{
            'name' = 'vcpkg-default-features-fail-require-other-feature';
            'default-features' = $False;
            'features' = @( 'success' )
        };
    }
}

New-Item -Path $manifestDir -ItemType Directory
$manifestDir = (Get-Item $manifestDir).FullName
New-Item -Path "$manifestDir/vcpkg.json" -ItemType File `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

Write-Trace "test manifest features: default-features, features = []"
Run-Vcpkg install @manifestDirArgs
Throw-IfNotFailed

Write-Trace "test manifest features: no-default-features, features = []"
Run-Vcpkg install @manifestDirArgs --x-no-default-features
Throw-IfFailed
Write-Trace "test manifest features: default-features, features = [core]"
Run-Vcpkg install @manifestDirArgs --x-feature=core
Throw-IfFailed
# test having both
Write-Trace "test manifest features: no-default-features, features = [core]"
Run-Vcpkg install @manifestDirArgs --x-no-default-features --x-feature=core
Throw-IfFailed

Write-Trace "test manifest features: no-default-features, features = [default-fail]"
Run-Vcpkg install @manifestDirArgs --x-no-default-features --x-feature=default-fail
Throw-IfNotFailed
Write-Trace "test manifest features: default-features, features = [core, default-fail]"
Run-Vcpkg install @manifestDirArgs --x-feature=core --x-feature=default-fail
Throw-IfNotFailed

Write-Trace "test manifest features: no-default-features, features = [copied-feature]"
Run-Vcpkg install @noDefaultFeatureArgs --x-feature=copied-feature
Throw-IfFailed
Write-Trace "test manifest features: no-default-features, features = [copied-feature, copied-feature]"
Run-Vcpkg install @noDefaultFeatureArgs --x-feature=copied-feature --x-feature=copied-feature
Throw-IfFailed

Write-Trace "test manifest features: no-default-features, features = [multiple-dep-1, multiple-dep-2]"
Run-Vcpkg install @noDefaultFeatureArgs --x-feature=multiple-dep-1 --x-feature=multiple-dep-2
Throw-IfFailed

Write-Trace "test manifest features: no-default-features, features = [no-default-features-1]"
Run-Vcpkg install @noDefaultFeatureArgs --x-feature=no-default-features-1
Throw-IfFailed
Write-Trace "test manifest features: no-default-features, features = [no-default-features-2]"
Run-Vcpkg install @noDefaultFeatureArgs --x-feature=no-default-features-2
Throw-IfFailed

$vcpkgJson = @{
    'name' = "manifest-test";
    'version' = "1.0.0";
    'features' = @{
        'a' = feature 'manifest-test';
    }
}

Set-Content -Path "$manifestDir/vcpkg.json" `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson) `
    -Encoding Ascii -NoNewline

$vcpkgJson = @{
    'name' = "manifest-test";
    'version' = "1.0.0";
    'dependencies' = @( "nonexistent-port" )
}

New-Item -Path $manifestDir/manifest-test -ItemType Directory
Set-Content -Path "$manifestDir/manifest-test/vcpkg.json" `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson) `
    -Encoding Ascii -NoNewline

Write-Trace "test manifest features: self-reference, features = [a]"
Run-Vcpkg install @manifestDirArgs --x-feature=a
Throw-IfFailed

Write-Trace "test manifest features: self-reference, features = [a], with overlay"
Run-Vcpkg install @manifestDirArgs --x-feature=a "--overlay-ports=$manifestDir/manifest-test"
Throw-IfFailed
