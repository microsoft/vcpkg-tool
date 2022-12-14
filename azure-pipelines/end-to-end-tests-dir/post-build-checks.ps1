. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (-not $IsWindows) {
	return
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e_ports" vcpkg-internal-dll-with-no-exports --no-binarycaching
$hasDebugDll = $buildOutput -match "packages/vcpkg-internal-dll-with-no-exports_$Triplet/debug/bin/no_exports\.dll"
$hasReleaseDll = $buildOutput -match "packages/vcpkg-internal-dll-with-no-exports_$Triplet/bin/no_exports\.dll"
$hasSet = $buildOutput -match 'set\(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled\)'
if (-not $hasDebugDll -or -not $hasReleaseDll -or -not $hasSet) {
	throw "Did not detect DLLs with no exports."
}
