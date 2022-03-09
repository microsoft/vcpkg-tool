[CmdletBinding(PositionalBinding = $False, DefaultParameterSetName = 'Tarball')]
Param(
    [Parameter(Mandatory = $True, ParameterSetName = 'Tarball')]
    [string]$DestinationTarball,
    [Parameter(Mandatory = $True, ParameterSetName = 'Directory')]
    [string]$DestinationDir,
    [Parameter(Mandatory = $True)]
    [string]$TempDir,
    [Parameter()]
    [switch]$ReadOnly,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AdditionalFiles
)

$AdditionalFilesNames = New-Object string[] $AdditionalFiles.Length
for ($idx = 0; $idx -ne $AdditionalFiles.Length; $idx++) {
    $raw = $AdditionalFiles[$idx]
    if (-not (Test-Path $raw)) {
        Write-Error "'$raw' did not exist."
        throw
    }

    $itemized = Get-Item $raw
    $AdditionalFiles[$idx] = $itemized.FullName
    $AdditionalFilesNames[$idx] = $itemized.Name
}

$sha = Get-Content "$PSScriptRoot/vcpkg-scripts-sha.txt" -Raw
$sha = $sha.Trim()

$scripts_dependencies = @(
    'addPoshVcpkgToPowershellProfile.ps1',
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
        Move-Item 'triplets' '../out/triplets'
        foreach ($dep in $scripts_dependencies) {
            Move-Item "scripts/$dep" "../out/scripts/$dep"
        }
    }
    finally {
        Pop-Location
    }

    for ($idx = 0; $idx -ne $AdditionalFiles.Length; $idx++) {
        Copy-Item -Path $AdditionalFiles[$idx] -Destination "out/$($AdditionalFilesNames[$idx])"
    }

    $bundleConfig = @{
        'readonly'       = [bool]$ReadOnly;
        'usegitregistry' = $True;
        'embeddedsha'    = $sha
    }

    New-Item -Path "out/.vcpkg-root" -ItemType "File"
    Set-Content -Path "out/vcpkg-bundle.json" `
        -Value (ConvertTo-Json -InputObject $bundleConfig) `
        -Encoding Ascii

    if ($DestinationTarball) {
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
