. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$candidateMissingTriplets = @(
    $TripletStatic,
    $Triplet,
    'x64-windows',
    'x64-linux',
    'x64-osx'
)

$distinctMissingTriplets = @()
foreach ($candidateMissingTriplet in $candidateMissingTriplets)
{
    if ($candidateMissingTriplet -ne $HostE2ETriplet -and -not $distinctMissingTriplets.Contains($candidateMissingTriplet))
    {
        $distinctMissingTriplets += $candidateMissingTriplet
    }
}

$firstMissingTriplet = $distinctMissingTriplets[0]
$secondMissingTriplet = $distinctMissingTriplets[1]

$removeArgs = $commonArgs + @(
    "--overlay-ports=$PSScriptRoot/../e2e-ports",
    "--overlay-triplets=$PSScriptRoot/../overlay-triplets"
)

Run-Vcpkg install @removeArgs "vcpkg-empty-port:$HostE2ETriplet"
Throw-IfFailed

$warning = Run-VcpkgAndCaptureOutput ($removeArgs + @(
    'remove',
    '--dry-run',
    "vcpkg-empty-port:$firstMissingTriplet",
    "vcpkg-empty-port:$secondMissingTriplet"
))
Throw-IfFailed
Throw-IfNonEndsWith -Actual $warning -Expected "warning: vcpkg-empty-port:$firstMissingTriplet is not installed, but vcpkg-empty-port is installed for $HostE2ETriplet. Did you mean vcpkg-empty-port:${HostE2ETriplet}?`n"
