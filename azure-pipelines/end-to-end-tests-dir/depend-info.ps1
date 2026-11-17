. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$dependInfoArgs = $commonArgs + @(
    "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports",
    "--overlay-triplets=$PSScriptRoot/../overlay-triplets"
)
$manifestRoot = "--x-manifest-root=$PSScriptRoot/../e2e-projects/depend-info"
$schemaFile = (Get-Item "$PSScriptRoot/../../docs/depend-info.schema.json").FullName

# Returns one line per package, sorted, for comparing the interesting parts of the JSON output
function Get-DependInfoSummary([string]$Json) {
    if (-not (Test-Json -Json $Json -SchemaFile $schemaFile)) {
        throw "depend-info output does not match the schema"
    }

    $packages = $Json | ConvertFrom-Json -AsHashtable
    $lines = [string[]]@(foreach ($key in $packages.Keys) {
        $package = $packages[$key]
        $dependencies = [string[]]@(foreach ($dependency in $package['dependencies']) {
            $line = "$($dependency['name']):$($dependency['triplet'])"
            if ($dependency['host']) {
                $line += ' (host)'
            }
            $line
        })
        [Array]::Sort($dependencies, [StringComparer]::Ordinal)
        $license = $package['license']
        if ($null -eq $license) {
            $license = 'null'
        }
        "$key host-only=$($package['host-only']) license=$license features=$($package['features'] -join ',') dependencies=[$($dependencies -join ', ')]"
    })
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    return $lines -join "`n"
}

# Host triplet == target triplet: host dependencies are installed for the target triplet, but are still host-only.
# vcpkg-depend-info-both is a host and a non-host dependency, vcpkg-depend-info-platform is only a non-host dependency if
# the triplet isn't native.
$expectedNative = @"
vcpkg-depend-info-both:$Triplet host-only=False license=BSL-1.0 features= dependencies=[]
vcpkg-depend-info-platform:$Triplet host-only=True license=MIT features= dependencies=[]
vcpkg-depend-info-root:$Triplet host-only=False license=Apache-2.0 AND MIT features=apache dependencies=[vcpkg-depend-info-both:$Triplet, vcpkg-depend-info-platform:$Triplet (host), vcpkg-depend-info-tool:$Triplet (host), vcpkg-license-features:$Triplet, vcpkg-license-mit:$Triplet, vcpkg-license-null:$Triplet]
vcpkg-depend-info-tool:$Triplet host-only=True license=null features= dependencies=[vcpkg-license-bsd:$Triplet]
vcpkg-license-bsd:$Triplet host-only=True license=BSD-3-Clause features= dependencies=[]
vcpkg-license-features:$Triplet host-only=False license=BSD-3-Clause features=bsd-3-clause dependencies=[]
vcpkg-license-mit:$Triplet host-only=False license=MIT features= dependencies=[]
vcpkg-license-null:$Triplet host-only=False license=LicenseRef-vcpkg-null features= dependencies=[]
"@.Replace("`r`n", "`n")

$expectedCross = @"
vcpkg-depend-info-both:$Triplet host-only=False license=BSL-1.0 features= dependencies=[]
vcpkg-depend-info-both:$HostE2ETriplet host-only=True license=BSL-1.0 features= dependencies=[]
vcpkg-depend-info-platform:$Triplet host-only=False license=MIT features= dependencies=[]
vcpkg-depend-info-platform:$HostE2ETriplet host-only=True license=MIT features= dependencies=[]
vcpkg-depend-info-root:$Triplet host-only=False license=Apache-2.0 AND MIT features=apache dependencies=[vcpkg-depend-info-both:$Triplet, vcpkg-depend-info-both:$HostE2ETriplet (host), vcpkg-depend-info-platform:$Triplet, vcpkg-depend-info-platform:$HostE2ETriplet (host), vcpkg-depend-info-tool:$HostE2ETriplet (host), vcpkg-license-features:$Triplet, vcpkg-license-mit:$Triplet, vcpkg-license-null:$Triplet]
vcpkg-depend-info-tool:$HostE2ETriplet host-only=True license=null features= dependencies=[vcpkg-license-bsd:$HostE2ETriplet]
vcpkg-license-bsd:$HostE2ETriplet host-only=True license=BSD-3-Clause features= dependencies=[]
vcpkg-license-features:$Triplet host-only=False license=BSD-3-Clause features=bsd-3-clause dependencies=[]
vcpkg-license-mit:$Triplet host-only=False license=MIT features= dependencies=[]
vcpkg-license-null:$Triplet host-only=False license=LicenseRef-vcpkg-null features= dependencies=[]
"@.Replace("`r`n", "`n")

# Classic mode
$output = Run-VcpkgAndCaptureOutput @dependInfoArgs depend-info --format=json "vcpkg-depend-info-root[apache]" --host-triplet $Triplet
Throw-IfFailed
Throw-IfNonEqual -Actual (Get-DependInfoSummary $output) -Expected $expectedNative

$root = ($output | ConvertFrom-Json -AsHashtable)["vcpkg-depend-info-root:$Triplet"]
Throw-IfNonEqual -Actual $root['version'] -Expected '1.0'
Throw-IfNonEqual -Actual $root['port-version'] -Expected 2
Throw-IfNonEqual -Actual $root['depth'] -Expected 0
Throw-IfNonEqual -Actual $root['homepage'] -Expected 'https://example.com/vcpkg-depend-info-root'

$output = Run-VcpkgAndCaptureOutput @dependInfoArgs depend-info --format=json "vcpkg-depend-info-root[apache]" --host-triplet $HostE2ETriplet
Throw-IfFailed
Throw-IfNonEqual -Actual (Get-DependInfoSummary $output) -Expected $expectedCross

$output = Run-VcpkgAndCaptureOutput @dependInfoArgs depend-info --format=json "vcpkg-depend-info-root[apache]" --host-triplet $Triplet --max-recurse=0
Throw-IfFailed
Throw-IfNonEqual -Actual ((($output | ConvertFrom-Json -AsHashtable).Keys) -join ',') -Expected "vcpkg-depend-info-root:$Triplet"

# Manifest mode: the manifest requests vcpkg-depend-info-tool as a host dependency
$output = Run-VcpkgAndCaptureOutput @dependInfoArgs $manifestRoot depend-info --format=json --host-triplet $Triplet
Throw-IfFailed
Throw-IfNonEqual -Actual (Get-DependInfoSummary $output) -Expected $expectedNative

$output = Run-VcpkgAndCaptureOutput @dependInfoArgs $manifestRoot depend-info --format=json --host-triplet $HostE2ETriplet
Throw-IfFailed
Throw-IfNonEqual -Actual (Get-DependInfoSummary $output) -Expected $expectedCross

# --show-depth is not supported for JSON
$output = Run-VcpkgAndCaptureStdErr @dependInfoArgs depend-info --format=json --show-depth vcpkg-depend-info-root
Throw-IfNotFailed
Throw-IfNonContains -Actual $output -Expected 'error: --show-depth can only be used with `list` and `tree` formats.'
