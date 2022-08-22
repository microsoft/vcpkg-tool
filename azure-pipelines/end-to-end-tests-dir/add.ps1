. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$manifestDir = "$TestingRoot/add_port"
$manifestDirArgs = $commonArgs + @("--x-manifest-root=$manifestDir")
$manifestPath = "$manifestDir/vcpkg.json"
$vcpkgJson = @{}

New-Item -Path $manifestDir -ItemType Directory
New-Item -Path $manifestPath -ItemType File `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

Run-Vcpkg add port zlib @manifestDirArgs
Throw-IfFailed

$expected = @"
{
  "dependencies": [
    "zlib"
  ]
}
"@

$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "Add port didn't add zlib dependency correctly.`nExpected: $expected`nActual:$actual"
}
