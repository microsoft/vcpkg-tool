. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "VCPKG_ROOT"

$targetMessage = 'ignoring mismatched VCPKG_ROOT environment value'

$defaultOutput = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @('install', 'vcpkg-empty-port'))
Throw-IfFailed
if ($defaultOutput.Contains($targetMessage)) {
	throw 'Expected no warning about VCPKG_ROOT when using the environment variable.'
}

$actualVcpkgRoot = $env:VCPKG_ROOT

pushd $actualVcpkgRoot
try {
	$samePathOutput = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @('install', 'vcpkg-empty-port'))
	Throw-IfFailed
	if ($samePathOutput.Contains($targetMessage)) {
		throw 'Expected no warning about VCPKG_ROOT when the detected path is the same as the configured path.'
	}

	$env:VCPKG_ROOT = Join-Path $actualVcpkgRoot 'ports' # any existing directory that isn't the detected root
	$differentPathOutput = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @('install', 'vcpkg-empty-port', '--debug'))
	Throw-IfFailed
	if (-not ($differentPathOutput.Contains($targetMessage))) {
		throw 'Expected a warning about VCPKG_ROOT differing when the detected path differs from the configured path.'
	}

	$setWithArgOutput = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @('install', 'vcpkg-empty-port', '--vcpkg-root', $actualVcpkgRoot, '--debug'))
	Throw-IfFailed
	if ($setWithArgOutput.Contains($targetMessage)) {
		throw 'Expected no warning about VCPKG_ROOT when the path is configured with a command line argument.'
	}
} finally {
	$env:VCPKG_ROOT = $actualVcpkgRoot
	popd
}
