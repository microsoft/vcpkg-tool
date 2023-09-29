. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Write-Trace "test successfully parsing good inputs"
$formatManifestAssets = (Get-Item "$PSScriptRoot/../e2e-assets/format-manifest").FullName
$testProjects = Get-ChildItem "$formatManifestAssets/*.json" -File
$testProjects | % {
    $asItem = Get-Item $_
    $full = $asItem.FullName
    $name = $asItem.Name
    $expectedPath = "$formatManifestAssets/expected/$name"
    $tempItemPath = "$TestingRoot/$name"
    Write-Trace "test that format-manifest on $full produces $expectedPath"
    [string]$expected = Get-Content $expectedPath -Raw
    Copy-Item $asItem $tempItemPath
    Run-Vcpkg format-manifest $tempItemPath
    $actual = Get-Content $tempItemPath -Raw
    if ($expected -ne $actual) {
        throw "Expected formatting $full to produce $expectedPath but was $tempItemPath"
    }
}

Write-Trace "test not crashing parsing malformed inputs"
$formatManifestEvilAssets = (Get-Item "$PSScriptRoot/../e2e-assets/format-manifest-bad").FullName
$testEvilProjects = Get-ChildItem "$formatManifestEvilAssets/*.json" -File
$testEvilProjects | % {
    $asItem = Get-Item $_
    $full = $asItem.FullName
    Write-Trace "test that format-manifest on $full produces an error"
    $output = Run-VcpkgAndCaptureOutput format-manifest $full
    Throw-IfNotFailed
    if ($output -match 'vcpkg has crashed') {
        throw "vcpkg crashed parsing $full"
    }

    $fullEscaped = [System.Text.RegularExpressions.Regex]::Escape($full)
    if ($output -notmatch 'error: while loading ' + $fullEscaped `
       -or $output -notmatch 'error: Failed to parse manifest file: ' + $fullEscaped) {
        throw "error not detected in $full"
    }

    if ($output -match 'mismatched type:') {
        throw "duplicate mismatched type error"
    }
}

Write-Trace "test re-serializing every manifest"
$manifestDir = "$TestingRoot/manifest-dir"
Copy-Item -Path "$env:VCPKG_ROOT/ports" -Destination $manifestDir -recurse -Force -Filter vcpkg.json
git init $manifestDir
Throw-IfFailed
git -C $manifestDir config user.name vcpkg-test
Throw-IfFailed
git -C $manifestDir config user.email my@example.com
Throw-IfFailed
git -C $manifestDir config core.autocrlf false
Throw-IfFailed
git -C $manifestDir add .
Throw-IfFailed
git -C $manifestDir commit -m "baseline"
Throw-IfFailed
Run-Vcpkg format-manifest --all --x-builtin-ports-root=$manifestDir/ports
Throw-IfFailed
$diff = (& git -C $manifestDir diff) | Out-String
if ($diff.length -gt 0) {
    throw "Expected formatting of manifests vcpkg.json to cause no modifications: \n$diff"
}
