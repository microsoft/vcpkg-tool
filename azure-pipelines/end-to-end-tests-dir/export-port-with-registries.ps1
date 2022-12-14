. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

### <Initialize registry>
# Creates a git registry to run the e2e tests on
$e2eProjects = "$PSScriptRoot/../e2e_projects"
$manifestRoot = "$e2eProjects/registries-export-port"
$e2eRegistryPath = "$PSScriptRoot/../e2e_registry".Replace('\', '\\')
$testOutput = "$TestingRoot/exported-ports"
Push-Location $e2eRegistryPath
try
{
	Write-Host "Initializing test registry"
	if (Test-Path "$e2eRegistryPath/.git")
	{
		Remove-Item -Recurse -Force "$e2eRegistryPath/.git"
	}
	

	$gitConfig = @(
		'-c', 'user.name=Nobody',
		'-c', 'user.email=nobody@example.com',
		'-c', 'core.autocrlf=false'
	)

	git @gitConfig init . | Out-Null
	Throw-IfFailed
	git @gitConfig add -A | Out-Null
	Throw-IfFailed
	git @gitConfig commit -m "initial commit" | Out-Null
	Throw-IfFailed
	$e2eRegistryBaseline = git rev-parse HEAD
	Throw-IfFailed
}
finally 
{
	Pop-Location
}
### </Initialize Registry>

$commonArgs += @("--x-manifest-root=$manifestRoot")

# [x-export-port] Export registry port at baseline
Refresh-TestRoot
$inFile = "$manifestRoot/vcpkg.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

Run-Vcpkg -EndToEndTestSilent @commonArgs x-export-port bar "$testOutput/bar" | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testOutput/bar" "$e2eRegistryPath/ports/bar"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export registry port at baseline"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Export registry port with version
Refresh-TestRoot
$inFile = "$manifestRoot/vcpkg.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

Run-Vcpkg -EndToEndTestSilent @commonArgs x-export-port foo@1.1.0 "$testOutput/foo" | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testOutput/foo" "$e2eRegistryPath/extra-ports/foo"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export registry port with version"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Export registry port with non-existing version
Refresh-TestRoot
$inFile = "$manifestRoot/vcpkg.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

$out = Run-VcpkgAndCaptureOutput -EndToEndTestSilent @commonArgs x-export-port foo@2.1.0 "$testOutput/foo"
Throw-IfNotFailed
if ($out -notmatch "Available versions:")
{
	Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export registry port with non-existing version"
    Write-Host $Script::CurrentTest
	$out
	throw "Expected message about available versions"
}

# [x-export-port] Export registry port with non-existing port
Refresh-TestRoot
$inFile = "$manifestRoot/vcpkg.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

$out = Run-VcpkgAndCaptureOutput -EndToEndTestSilent @commonArgs x-export-port fu "$testOutput/fu"
Throw-IfNotFailed
if ($out -notmatch "no registry configured")
{
	Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export registry port with non-existing port"
    Write-Host $Script::CurrentTest
	$out
	throw "Expected message about no registry configured for port"
}
