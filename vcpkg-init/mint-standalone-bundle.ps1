[CmdletBinding(PositionalBinding=$False)]
Param(
    [Parameter()]
    [string]$DestinationTarballName,
    [Parameter()]
    [string]$DestinationDir,
    [Parameter(Mandatory)]
    [string]$TempPath,
    [Parameter()]
    [switch]$readonly,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AdditionalFiles
)

if (-not ($DestinationTarballName -or $DestinationDir)) {
    Write-Error "Either DestinationTarballName or DestinationDir must be set."
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

if (Test-Path $TempPath) {
    rm -Recurse $TempPath
}

mkdir $TempPath
pushd $TempPath
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
        'readonly' = $readonly.isPresent;
        'usegitregistry' = $True;
        'embeddedsha' = $sha
    }
    New-Item -Path "out/.vcpkg-root" -ItemType "File"

    Set-Content -Path "out/vcpkg-bundle.json" `
        -Value (ConvertTo-Json -InputObject $bundleConfig) `
        -Encoding Ascii
    if ($DestinationDir) {
        if (-not (Test-Path $DestinationDir)) {
            mkdir $DestinationDir
        }
        Copy-Item -Recurse out/* $DestinationDir
    } else {
        tar czf $DestinationTarballName -C out *
    }
}
finally {
    popd
}

rm -Recurse $TempPath
