[CmdletBinding()]
Param(
    [Parameter(Mandatory = $True)]
    [string]$DestinationTarballName,
    [Parameter(Mandatory = $True)]
    [string]$BundleConfig,
    [Parameter()]
    [string]$TempPath
)

if (-not (Test-Path $BundleConfig)) {
    Write-Error 'BundleConfig must be an existing file.'
    throw
}

$BundleConfig = (Get-Item $BundleConfig).FullName

$sha = Get-Content "$PSScriptRoot/vcpkg-scripts-sha.txt" -Raw
$sha = $sha.Trim()

$scripts_dependencies = @(
    'buildsystems',
    'cmake',
    'detect_compiler',
    'get_cmake_vars',
    'posh-vcpkg',
    'templates',
    'toolchains',
    'addPoshVcpkgToPowershellProfile.ps1',
    'ports.cmake',
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

    cp $BundleConfig 'out/.vcpkg-root'
    tar czf $DestinationTarballName -C out *
}
finally {
    popd
}

rm -Recurse $TempPath
