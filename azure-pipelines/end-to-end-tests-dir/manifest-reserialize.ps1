. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Write-Trace "test re-serializing every manifest"
$manifestDir = "$TestingRoot/manifest-dir"
New-Item -Path $manifestDir -ItemType Directory | Out-Null

$ports = Get-ChildItem "$env:VCPKG_ROOT/ports"

$ports | % {
    if (Test-Path "$_/vcpkg.json") {
        Copy-Item "$_/vcpkg.json" "$manifestDir" | Out-Null
        $x = Get-Content "$manifestDir/vcpkg.json" -Raw
        Run-Vcpkg -EndToEndTestSilent format-manifest "$manifestDir/vcpkg.json" | Out-Null
        Throw-IfFailed "$_/vcpkg.json"
        $y = Get-Content "$manifestDir/vcpkg.json" -Raw
        if ($x -ne $y) {
            throw "Expected formatting manifest $_/vcpkg.json to cause no modifications"
        }
    }
}