. $PSScriptRoot/../end-to-end-tests-prelude.ps1

### <Initialize registry>
# Creates a git registry to run the e2e tests on
$e2eProjects = "$PSScriptRoot/../e2e_projects"
$manifestRoot = "$e2eProjects/registries-package-patterns"
$e2eRegistryPath = "$PSScriptRoot/../e2e_registry"
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

	git @gitConfig init -b main . | Out-Null
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


# [patterns] No patterns (no default)
Write-Host "[patterns] No patterns (no default)"
$inFile = "$manifestRoot/no-patterns.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

Run-Vcpkg -EndToEndTestSilent install @commonArgs "--x-manifest-root=$manifestRoot" | Out-Null
Throw-IfFailed
Refresh-TestRoot

# [patterns] Patterns only (no default)
Write-Host "[patterns] Patterns only (no default)"
$inFile = "$manifestRoot/only-patterns.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

Run-Vcpkg -EndToEndTestSilent install @commonArgs "--x-manifest-root=$manifestRoot" | Out-Null
Throw-IfFailed
Refresh-TestRoot

# [patterns] Patterns with default
Write-Host "[patterns] Patterns with default"
$inFile = "$manifestRoot/with-default.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

Run-Vcpkg -EndToEndTestSilent install @commonArgs "--x-manifest-root=$manifestRoot" | Out-Null
Throw-IfFailed
Refresh-TestRoot

# [patterns] Repeated patterns
Write-Host "[patterns] Repeated patterns"
$inFile = "$manifestRoot/with-redeclaration.json.in"
(Get-Content -Path "$inFile").Replace("`$E2ERegistryPath", $e2eRegistryPath).Replace("`$E2ERegistryBaseline", $e2eRegistryBaseline) `
| Out-File "$manifestRoot/vcpkg.json"

$out = Run-VcpkgAndCaptureOutput -EndToEndTestSilent install @commonArgs "--x-manifest-root=$manifestRoot"
Throw-IfFailed
if ($out -notmatch "redeclarations will be ignored")
{
	$out
	throw "Expected warning about redeclaration"
}
Refresh-TestRoot