. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$manifestDir = "$TestingRoot/add_port"
$manifestDirArgs = $commonArgs + @("--x-manifest-root=$manifestDir")
$manifestPath = "$manifestDir/vcpkg.json"
$vcpkgJson = @{}

New-Item -Path $manifestDir -ItemType Directory
New-Item -Path $manifestPath -ItemType File `
    -Value (ConvertTo-Json -Depth 5 -InputObject $vcpkgJson)

Run-Vcpkg add port "sqlite3[core]" "sqlite3[core]" @manifestDirArgs
Throw-IfFailed

$expected = @"
{
  "dependencies": [
    {
      "name": "sqlite3",
      "default-features": false
    }
  ]
}
"@

$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "Add port didn't add sqlite3[core] dependency correctly.`nExpected: $expected`nActual:$actual"
}

# Add default features
Run-Vcpkg add port "sqlite3" "sqlite3[core]" @manifestDirArgs
Throw-IfFailed

$expected = @"
{
  "dependencies": [
    "sqlite3"
  ]
}
"@

$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "Add port didn't add sqlite3 dependency correctly.`nExpected: $expected`nActual:$actual"
}

# Adding sqlite3[core] does not change something because default features are already enabled
Run-Vcpkg add port "sqlite3[core]" @manifestDirArgs
Throw-IfFailed
$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "Add port didn't add sqlite3[core] dependency correctly.`nExpected: $expected`nActual:$actual"
}

# Add zlib as a feature in comparison to the previous test
Run-Vcpkg add port "sqlite3[core,zlib]" @manifestDirArgs
Throw-IfFailed
$expected = @"
{
  "dependencies": [
    {
      "name": "sqlite3",
      "features": [
        "zlib"
      ]
    }
  ]
}
"@
$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "Add port didn't add sqlite3[zlib] dependency correctly.`nExpected: $expected`nActual:$actual"
}


