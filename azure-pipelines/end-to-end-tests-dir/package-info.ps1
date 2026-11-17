. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$portsRoot = "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports"

# Ports: success when every requested port is found
$output = Run-VcpkgAndCaptureOutput @commonArgs $portsRoot x-package-info --x-json vcpkg-license-bsd vcpkg-license-mit
Throw-IfFailed
Throw-IfNonEqual -Actual $output -Expected @"
{
  "results": {
    "vcpkg-license-bsd": {
      "name": "vcpkg-license-bsd",
      "version": "0",
      "license": "BSD-3-Clause"
    },
    "vcpkg-license-mit": {
      "name": "vcpkg-license-mit",
      "version": "0",
      "license": "MIT"
    }
  }
}

"@

# Ports: unknown ports are reported on stderr and fail the command, but found ports are still printed
$output = Run-VcpkgAndCaptureOutput @commonArgs $portsRoot x-package-info --x-json vcpkg-license-mit vcpkg-does-not-exist
Throw-IfNotFailed
Throw-IfNonEqual -Actual $output -Expected @"
{
  "results": {
    "vcpkg-license-mit": {
      "name": "vcpkg-license-mit",
      "version": "0",
      "license": "MIT"
    }
  }
}

"@

$output = Run-VcpkgAndCaptureStdErr @commonArgs $portsRoot x-package-info --x-json vcpkg-license-mit vcpkg-does-not-exist
Throw-IfNotFailed
Throw-IfNonContains -Actual $output -Expected "error: vcpkg-does-not-exist does not exist"

# Installed: success when every requested package is installed
Run-Vcpkg @commonArgs $portsRoot install vcpkg-license-bsd-on-mit
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @commonArgs $portsRoot x-package-info --x-json --x-installed "vcpkg-license-bsd-on-mit:$Triplet"
Throw-IfFailed
$results = ($output | ConvertFrom-Json -AsHashtable)['results']
Throw-IfNonEqual -Actual (($results.Keys | Sort-Object) -join ',') -Expected "vcpkg-license-bsd-on-mit:$Triplet"
Throw-IfNonEqual -Actual ($results["vcpkg-license-bsd-on-mit:$Triplet"]['dependencies'] -join ',') -Expected "vcpkg-license-mit:$Triplet"

$output = Run-VcpkgAndCaptureOutput @commonArgs $portsRoot x-package-info --x-json --x-installed --x-transitive "vcpkg-license-bsd-on-mit:$Triplet"
Throw-IfFailed
$results = ($output | ConvertFrom-Json -AsHashtable)['results']
Throw-IfNonEqual -Actual (($results.Keys | Sort-Object) -join ',') -Expected "vcpkg-license-bsd-on-mit:$Triplet,vcpkg-license-mit:$Triplet"

# Installed: packages which are not installed are reported on stderr and fail the command
$output = Run-VcpkgAndCaptureOutput @commonArgs $portsRoot x-package-info --x-json --x-installed "vcpkg-license-mit:$Triplet" "vcpkg-license-bsd:$Triplet"
Throw-IfNotFailed
$results = ($output | ConvertFrom-Json -AsHashtable)['results']
Throw-IfNonEqual -Actual (($results.Keys | Sort-Object) -join ',') -Expected "vcpkg-license-mit:$Triplet"

$output = Run-VcpkgAndCaptureStdErr @commonArgs $portsRoot x-package-info --x-json --x-installed "vcpkg-license-mit:$Triplet" "vcpkg-license-bsd:$Triplet"
Throw-IfNotFailed
Throw-IfNonContains -Actual $output -Expected "error: vcpkg-license-bsd:$Triplet is not installed."

Run-Vcpkg @commonArgs $portsRoot remove vcpkg-license-bsd-on-mit vcpkg-license-mit
Throw-IfFailed
