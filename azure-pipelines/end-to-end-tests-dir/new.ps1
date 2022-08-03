. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$manifestDir = "$TestingRoot/new_project"
New-Item -Path $manifestDir -ItemType Directory
$manifestPath = Join-Path $manifestDir 'vcpkg.json'
$configurationPath = Join-Path $manifestDir 'vcpkg-configuration.json'

Push-Location $manifestDir
$result = Run-Vcpkg new
Pop-Location
Throw-IfNotFailed
if (-not $result -contains '--application') {
    throw "New without --name or --version didn't require setting --application"
}

Push-Location $manifestDir
Run-Vcpkg new --name=hello --version=1.0
Pop-Location
Throw-IfFailed

$expected = @"
{
  "name": "hello",
  "version": "1.0"
}
"@

$actual = (Get-Content -Path $manifestPath -Raw).TrimEnd()
if ($expected -ne $actual) {
    throw "New didn't create vcpkg manifest correctly."
}

Push-Location $manifestDir
$result = Run-Vcpkg new --application
Pop-Location
Throw-IfNotFailed
if (-not $result -contains 'A manifest is already present at') {
    throw "New didn't detect existing manifest correctly"
}

Remove-Item $manifestPath
Push-Location $manifestDir
$result = Run-Vcpkg new --application
Pop-Location
Throw-IfNotFailed
if (-not $result -contains 'Creating a manifest would overwrite a vcpkg-configuration.json') {
    throw "New didn't detect existing configuration correctly"
}

Remove-Item $configurationPath
