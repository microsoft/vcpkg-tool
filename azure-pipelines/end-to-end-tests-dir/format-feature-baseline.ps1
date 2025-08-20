. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$ciFeatureBaselines = (Get-Item "$PSScriptRoot/../e2e-assets/format-feature-baseline").FullName
$testProjects = Get-ChildItem "$ciFeatureBaselines/*.txt" -File
$testProjects | % {
    $asItem = Get-Item $_
    $full = $asItem.FullName
    $name = $asItem.Name
    $expectedPath = "$ciFeatureBaselines/expected/$name"
    $tempItemPath = "$TestingRoot/$name"
    Write-Trace "test that format-feature-baseline on $full produces $expectedPath"
    [string]$expected = Get-Content $expectedPath -Raw
    Copy-Item $asItem $tempItemPath
    Run-Vcpkg format-feature-baseline $tempItemPath
    $actual = Get-Content $tempItemPath -Raw
    if ($expected -ne $actual) {
        throw "Expected formatting $full to produce $expectedPath but was $tempItemPath"
    }
}