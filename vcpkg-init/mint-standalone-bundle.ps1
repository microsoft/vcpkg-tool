[CmdletBinding(PositionalBinding = $False, DefaultParameterSetName = 'Tarball')]
Param(
    [Parameter(Mandatory = $True, ParameterSetName = 'Tarball')]
    [string]$DestinationTarball,
    [Parameter(Mandatory = $True, ParameterSetName = 'Directory')]
    [string]$DestinationDir,
    [Parameter(Mandatory = $True)]
    [string]$TempDir,
    [Parameter(Mandatory = $True)]
    [string]$Deployment,
    [Parameter(Mandatory = $True)]
    [string]$SignedFilesRoot,
    [Parameter(Mandatory = $true)]
    [string]$VcpkgBaseVersion
)

$sha = Get-Content "$PSScriptRoot/vcpkg-scripts-sha.txt" -Raw
$sha = $sha.Trim()

if ($Deployment -eq 'VisualStudio') {
    $BundleConfig = @{
        'readonly'       = $True;
        'usegitregistry' = $True;
        'embeddedsha'    = $sha;
        'deployment'     = $Deployment;
        'vsversion'      = "17.0";
    }
} else {
    $BundleConfig = @{
        'readonly'       = $False;
        'usegitregistry' = $True;
        'embeddedsha'    = $sha;
        'deployment'     = $Deployment;
    }
}

$scripts_dependencies = @(
    'build_info.cmake',
    'buildsystems',
    'cmake',
    'detect_compiler',
    'get_cmake_vars',
    'ports.cmake',
    'posh-vcpkg',
    'templates',
    'toolchains',
    'vcpkg_completion.bash',
    'vcpkg_completion.fish',
    'vcpkg_completion.zsh',
    'vcpkg_get_dep_info.cmake',
    'vcpkg_get_tags.cmake',
    'vcpkgTools.xml'
)

$scripts_exclusions = @(
    'buildsystems/msbuild/applocal.ps1',
    'posh-vcpkg/0.0.1/posh-vcpkg.psm1'
)

if (Test-Path $TempDir) {
    Remove-Item -Recurse $TempDir
}

New-Item -Path $TempDir -ItemType 'Directory' -Force
Push-Location $TempDir
try {
    $target = "https://github.com/microsoft/vcpkg/archive/$sha.zip"
    Write-Host $target
    & curl.exe -L -o repo.zip $target
    & tar xf repo.zip
    New-Item -Path 'out/scripts' -ItemType 'Directory' -Force
    Push-Location "vcpkg-$sha"
    try {
        Move-Item 'LICENSE.txt' '../out/LICENSE.txt'
        Move-Item 'triplets' '../out/triplets'
        foreach ($exclusion in $scripts_exclusions) {
            Remove-Item "scripts/$exclusion" -Recurse -Force
        }
        foreach ($dep in $scripts_dependencies) {
            Move-Item "scripts/$dep" "../out/scripts/$dep"
        }
    }
    finally {
        Pop-Location
    }

    Set-Content -Path "out/vcpkg-version.txt" -Value $VcpkgBaseVersion -NoNewLine -Encoding Ascii
    Copy-Item -Path "$PSScriptRoot/vcpkg-cmd.cmd" -Destination 'out/vcpkg-cmd.cmd'
    Copy-Item -Path "$SignedFilesRoot/vcpkg-init" -Destination 'out/vcpkg-init'
    Copy-Item -Path "$SignedFilesRoot/vcpkg-init.ps1" -Destination 'out/vcpkg-init.ps1'
    Copy-Item -Path "$SignedFilesRoot/vcpkg-init.cmd" -Destination 'out/vcpkg-init.cmd'
    Copy-Item -Path "$SignedFilesRoot/addPoshVcpkgToPowershellProfile.ps1" -Destination 'out/scripts/addPoshVcpkgToPowershellProfile.ps1'
    New-Item -Path 'out/scripts/buildsystems/msbuild' -ItemType 'Directory' -Force
    Copy-Item -Path "$SignedFilesRoot/applocal.ps1" -Destination 'out/scripts/buildsystems/msbuild/applocal.ps1'
    New-Item -Path 'out/scripts/posh-vcpkg/0.0.1' -ItemType 'Directory' -Force
    Copy-Item -Path "$SignedFilesRoot/posh-vcpkg.psm1" -Destination 'out/scripts/posh-vcpkg/0.0.1/posh-vcpkg.psm1'

    New-Item -Path "out/.vcpkg-root" -ItemType "File"
    Set-Content -Path "out/vcpkg-bundle.json" `
        -Value (ConvertTo-Json -InputObject $BundleConfig) `
        -Encoding Ascii

    if (-not [String]::IsNullOrEmpty($DestinationTarball)) {
        & tar czf $DestinationTarball -C out *
    }

    if (-not [String]::IsNullOrEmpty($DestinationDir)) {
        if (Test-Path $DestinationDir) {
            Remove-Item -Recurse $DestinationDir
        }

        $parent = [System.IO.Path]::GetDirectoryName($DestinationDir)
        if (-not [String]::IsNullOrEmpty($parent)) {
            New-Item -Path $parent -ItemType 'Directory' -Force
        }

        Move-Item out $DestinationDir
    }
}
finally {
    Pop-Location
}

Remove-Item -Recurse $TempDir
