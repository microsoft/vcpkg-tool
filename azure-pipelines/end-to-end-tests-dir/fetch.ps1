. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$VcpkgToolsJsonSchemaFile = (Get-Item "$PSScriptRoot/../../docs/vcpkg-tools.schema.json").FullName
if (-not (Test-Json -ea:0 -LiteralPath "$VcpkgRoot/scripts/vcpkg-tools.json" -SchemaFile $VcpkgToolsJsonSchemaFile)) {
    throw "real vcpkg-tools.json doesn't conform to schema"
}

if (-not $IsMacOS -and -not $IsLinux) {
    "" | Out-File -enc ascii $(Join-Path $TestingRoot .vcpkg-root)

    $Scripts = Join-Path $TestingRoot "scripts"
    mkdir $Scripts | Out-Null
     
    $VcpkgToolsJson = Join-Path $Scripts "vcpkg-tools.json"

    $7zip_version = "19.00"
    $ninja_version = "1.10.2"

    @'
{
    "schema-version": 1,
    "tools": [
    {
        "name": "ninja-testing",
        "os": "windows",
        "version": "1.10.2",
        "executable": "ninja.exe",
        "url": "https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-win.zip",
        "sha512": "6004140d92e86afbb17b49c49037ccd0786ce238f340f7d0e62b4b0c29ed0d6ad0bab11feda2094ae849c387d70d63504393714ed0a1f4d3a1f155af7a4f1ba3",
        "archive": "ninja-win-1.10.2.zip"
    },
    {
        "name": "ninja",
        "os": "windows",
        "version": "1.10.2",
        "executable": "ninja.exe",
        "url": "https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-win.zip",
        "sha512": "6004140d92e86afbb17b49c49037ccd0786ce238f340f7d0e62b4b0c29ed0d6ad0bab11feda2094ae849c387d70d63504393714ed0a1f4d3a1f155af7a4f1ba3",
        "archive": "ninja-win-1.10.2.zip"
    },
    {
        "name": "cmake",
        "os": "windows",
        "version": "3.22.2",
        "executable": "cmake-3.22.2-windows-i386\\bin\\cmake.exe",
        "url": "https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-windows-i386.zip",
        "sha512": "969d3d58d56d8fa3cc3acae2b949bf58abab945f70ae292ff20c9060d845dfc094c613c367a924abff47f307cc33af1467cdb9b75bb857868e38b2c7cdc72f79",
        "archive": "cmake-3.22.2-windows-i386.zip"
    },
    {
        "name": "cmake",
        "os": "osx",
        "version": "3.22.2",
        "executable": "cmake-3.22.2-macos-universal/CMake.app/Contents/bin/cmake",
        "url": "https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-macos-universal.tar.gz",
        "sha512": "08104f608ecb9a5cfef38e79f0957d21e425616c0677781445492f82cbfec805113e3b5eb4bc737b707bb26a00678e7bd55e17555a5611c08b0b9b44ac5136ac",
        "archive": "cmake-3.22.2-macos-universal.tar.gz"
    },
    {
        "name": "cmake",
        "os": "linux",
        "version": "3.22.2",
        "executable": "cmake-3.22.2-linux-x86_64/bin/cmake",
        "url": "https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-linux-x86_64.tar.gz",
        "sha512": "579e08b086f6903ef063697fca1dc2692f68a7341dd35998990b772b4221cdb5b1deecfa73bad9d46817ef09e58882b2adff9d64f959c01002c11448a878746b",
        "archive": "cmake-3.22.2linux-x86_64.tar.gz"
    },
    {
        "name": "cmake",
        "os": "freebsd",
        "version": "3.20.4",
        "executable": "/usr/local/bin/cmake",
        "url": "https://pkg.freebsd.org/FreeBSD:13:amd64/quarterly/All/cmake-3.20.4.txz",
        "sha512": "3e5b675d7ff924f92996d912e2365582e687375109ef99c9073fb8196bb329243a406b218cf1358d7cc518988b311ce9e5bf87de4d64f2e6377b7c2bc8894475",
        "archive": "cmake-3.20.4.txz"
    }]
}
'@ | % { $_ -replace "`r","" } | Out-File -enc ascii $VcpkgToolsJson

    if (-not (Test-Json -ea:0 -LiteralPath $VcpkgToolsJson -SchemaFile $VcpkgToolsJsonSchemaFile)) {
        throw "testing vcpkg-tools.json doesn't conform to schema"
    }

    $env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'down loads'
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja-testing", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$env:VCPKG_DOWNLOADS/tools/ninja-testing-1.10.2-windows/ninja.exe"

    $path = $env:PATH

    $env:PATH = "$path;$env:VCPKG_DOWNLOADS/tools/ninja-testing-1.10.2-windows"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$env:VCPKG_DOWNLOADS/tools/ninja-1.10.2-windows/ninja.exe"

    $env:VCPKG_FORCE_DOWNLOADED_BINARIES = "1"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$env:VCPKG_DOWNLOADS/tools/ninja-1.10.2-windows/ninja.exe"

    Remove-Item -Recurse -Force "$env:VCPKG_DOWNLOADS/tools/ninja-1.10.2-windows" -ErrorAction SilentlyContinue
    Remove-Item env:VCPKG_FORCE_DOWNLOADED_BINARIES

    $env:VCPKG_FORCE_SYSTEM_BINARIES = "1"
    $env:PATH = "$PSScriptRoot\..\e2e-assets\fetch;$path"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$env:VCPKG_DOWNLOADS/tools/ninja-1.10.2-windows/ninja.exe"

    Remove-Item env:VCPKG_FORCE_SYSTEM_BINARIES
    $out = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot", "--x-stderr-status"))
    Throw-IfFailed
    & $out --version
    if ($LASTEXITCODE -ne 0) {
        throw 'Couldn''t run resulting ninja'
    }
}

# Concurrent `vcpkg fetch ninja` invocations must all succeed: each process
# extracts into a per-PID partial directory before atomically renaming into
# place. Regression test for microsoft/vcpkg#50051. The archive is served from a
# local asset cache so the eight concurrent jobs stay offline; they share a
# downloads directory so they genuinely race on the extraction.
$env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'concurrent-fetch-downloads'
Remove-Item -Recurse -Force $env:VCPKG_DOWNLOADS -ErrorAction SilentlyContinue
$ninjaAssetSource = "x-azurl,file://$AssetCache,,readwrite"

# Populate the asset cache from the authoritative source once, then clear the
# downloads so each concurrent job re-fetches the archive from the cache offline.
Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--x-stderr-status", "--x-asset-sources=$ninjaAssetSource"))
Throw-IfFailed
Remove-Item -Recurse -Force $env:VCPKG_DOWNLOADS -ErrorAction SilentlyContinue

$jobs = @()
for ($i = 0; $i -lt 8; $i++) {
    $jobs += Start-Job -ScriptBlock {
        param($vcpkg, $vcpkgArgs)
        & $vcpkg @vcpkgArgs | Out-Null
        $LASTEXITCODE
    } -ArgumentList @($VcpkgExe, ($commonArgs + @("fetch", "ninja", "--x-stderr-status", "--x-asset-sources=$ninjaAssetSource;x-block-origin")))
}

$exitCodes = @($jobs | Wait-Job | Receive-Job)
$jobs | Remove-Job
$failed = @($exitCodes | Where-Object { $_ -ne 0 })
if ($failed.Count -gt 0) {
    throw "Concurrent vcpkg fetch ninja: $($failed.Count) of $($exitCodes.Count) invocations failed (regression of microsoft/vcpkg#50051)"
}
