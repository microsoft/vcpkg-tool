. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$secondMissingTriplet = $TripletStatic
if ($secondMissingTriplet -eq $Triplet -or $secondMissingTriplet -eq $HostE2ETriplet)
{
    $secondMissingTriplet = 'x64-windows'
}

$removeArgs = $commonArgs + @(
    "--overlay-ports=$PSScriptRoot/../e2e-ports",
    "--overlay-triplets=$PSScriptRoot/../overlay-triplets"
)

Run-Vcpkg install @removeArgs "vcpkg-empty-port:$HostE2ETriplet"
Throw-IfFailed

$warning = Run-VcpkgAndCaptureStdErr -TestArgs ($removeArgs + @(
    'remove',
    '--dry-run',
    "vcpkg-empty-port:$Triplet",
    "vcpkg-empty-port:$secondMissingTriplet"
))
Throw-IfFailed
Throw-IfNonEqual -Actual $warning -Expected "warning: vcpkg-empty-port:$Triplet is not installed, but vcpkg-empty-port is installed for $HostE2ETriplet. Did you mean vcpkg-empty-port:${HostE2ETriplet}?`n"
