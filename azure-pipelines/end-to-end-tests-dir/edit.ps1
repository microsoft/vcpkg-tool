. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$expected = "$env:VCPKG_ROOT/ports/zlib`n$env:VCPKG_ROOT/ports/zlib/portfile.cmake`n-n"
$expected = $expected.Replace('\', '/')

Refresh-TestRoot

$buildDir = (Get-Item $VcpkgExe).Directory
$tempFilePath = "$TestingRoot/result.txt"

$env:VCPKG_TEST_OUTPUT = $tempFilePath
$editor = "$buildDir/test-editor"
if ($IsWindows) {
	$editor += '.exe'
}

Write-Host "Using editor $editor"
$env:EDITOR = $editor
try {
	Run-Vcpkg edit zlib
	Throw-IfFailed

	$result = Get-Content -LiteralPath $tempFilePath -Raw
} finally {
	Remove-Item env:VCPKG_TEST_OUTPUT
	Remove-Item env:EDITOR
}

$result = $result.Trim().Replace('\', '/')

if ($result -ne $expected) {
	throw 'Did not edit the expected directory.'
}