$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$vcpkgRoot = $PSScriptRoot
$vcpkgExe = Join-Path $vcpkgRoot 'vcpkg.exe'
$binaryCache = Join-Path $vcpkgRoot 'bincache'
$triplet = switch ($env:PROCESSOR_ARCHITECTURE) {
    'AMD64' { 'x64-windows' }
    'ARM64' { 'arm64-windows' }
    default { 'x86-windows' }
}

function Initialize-VcpkgEnv {
    [CmdletBinding()]
    param()
    
    $env:VCPKG_ROOT = $vcpkgRoot
    $env:VCPKG_DEFAULT_TRIPLET = $triplet
    $env:VCPKG_BINARY_CACHE = $binaryCache
    $env:VCPKG_KEEP_ENV_VARS = "PATH;_PATH;TEMP;TMP"
    $env:PATH = "$vcpkgRoot;$env:PATH"
    
    if (-not (Test-Path $binaryCache)) {
        $null = New-Item -ItemType Directory -Path $binaryCache -Force
    }

    if (-not (Test-Path $vcpkgExe)) {
        $bootstrapScript = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
        & $bootstrapScript
        if ($LASTEXITCODE -ne 0) { throw "Failed to bootstrap vcpkg" }
    }
}

function Install-VcpkgPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, ValueFromPipeline)]
        [string[]]$PackageName,
        [switch]$Clean
    )
    
    process {
        foreach ($package in $PackageName) {
            $arguments = @('install', $package)
            if ($Clean) { $arguments += '--clean-after-build' }
            & $vcpkgExe $arguments
            if ($LASTEXITCODE -ne 0) { throw "Failed to install package: $package" }
        }
    }
}

function Update-VcpkgIndex {
    [CmdletBinding()]
    param([switch]$Force)
    
    $arguments = @('update')
    if ($Force) { $arguments += '--no-dry-run' }
    & $vcpkgExe $arguments
    if ($LASTEXITCODE -ne 0) { throw "Failed to update vcpkg index" }
}

function Remove-VcpkgPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, ValueFromPipeline)]
        [string[]]$PackageName
    )
    
    process {
        foreach ($package in $PackageName) {
            & $vcpkgExe remove $package
            if ($LASTEXITCODE -ne 0) { throw "Failed to remove package: $package" }
        }
    }
}

Initialize-VcpkgEnv 