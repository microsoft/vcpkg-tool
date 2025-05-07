. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

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

$output = Run-VcpkgAndCaptureOutput @commonArgs format-manifest "$PSScriptRoot/../e2e-assets/format-manifest-malformed/bad-feature-name.json"
Throw-IfNotFailed
$expected = "bad-feature-name.json: error: $.features.b_required (a feature): features must be lowercase alphanumeric+hyphens, and not one of the reserved names"
Throw-IfNonContains -Expected $expected -Actual $output

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
