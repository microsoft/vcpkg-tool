. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$formatManifestAssets = (Get-Item "$PSScriptRoot/../e2e_assets/format-manifest").FullName
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
    Run-Vcpkg format-manifest $tempItemPath | Out-Null
    $actual = Get-Content $tempItemPath -Raw
    if ($expected -ne $actual) {
        throw "Expected formatting $full to produce $expectedPath but was $tempItemPath"
    }
}

Write-Trace "test re-serializing every manifest"
$manifestDir = "$TestingRoot/manifest-dir"
New-Item -Path $manifestDir -ItemType Directory | Out-Null

$ports = Get-ChildItem "$env:VCPKG_ROOT/ports"

$ports | % {
    if (($_ | Split-Path -leaf) -in @("libuvc", "mlpack", "qt5-virtualkeyboard")) {
        return
    }
    Copy-Item "$_/vcpkg.json" "$manifestDir" | Out-Null
    $x = Get-Content "$manifestDir/vcpkg.json" -Raw
    Run-Vcpkg -EndToEndTestSilent format-manifest "$manifestDir/vcpkg.json" | Out-Null
    Throw-IfFailed "$_/vcpkg.json"
    $y = Get-Content "$manifestDir/vcpkg.json" -Raw
    if ($x -ne $y) {
        throw "Expected formatting manifest $_/vcpkg.json to cause no modifications"
    }
}
