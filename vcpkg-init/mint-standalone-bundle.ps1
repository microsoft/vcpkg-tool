[CmdletBinding(PositionalBinding = $False)]
Param(
    [Parameter()]
    [string]$DestinationTarball,
    [Parameter()]
    [string]$DestinationDir,
    [Parameter(Mandatory = $True)]
    [string]$TempDir,
    [Parameter()]
    [switch]$ReadOnly,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AdditionalFiles
)

if (-not ($DestinationTarball -or $DestinationDir)) {
    Write-Error "Either DestinationTarball or DestinationDir must be set."
    throw
}

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
    rm -Recurse $TempDir
}

mkdir $TempDir
pushd $TempDir
try {
    $target = "https://github.com/microsoft/vcpkg/archive/$sha.zip"
    Write-Host $target
    curl.exe -L -o repo.zip $target
    tar xf repo.zip
    mkdir out
    mkdir 'out/scripts'
    pushd "vcpkg-$sha"
    try {
        mv 'triplets' '../out/triplets'
        foreach ($dep in $scripts_dependencies) {
            mv "scripts/$dep" "../out/scripts/$dep"
        }
    }
    finally {
        popd
    }

    for ($idx = 0; $idx -ne $AdditionalFiles.Length; $idx++) {
        cp $AdditionalFiles[$idx] "out/$($AdditionalFilesNames[$idx])"
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
        tar czf $DestinationTarball -C out *
    }

    if ($DestinationDir) {
        if (Test-Path $DestinationDir) {
            rm -Recurse $DestinationDir
        }

        mv out $DestinationDir
    }
}
finally {
    popd
}

rm -Recurse $TempDir
