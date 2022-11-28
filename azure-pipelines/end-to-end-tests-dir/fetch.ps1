. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (-not $IsMacOS -and -not $IsLinux) {
    "" | Out-File -enc ascii $(Join-Path $TestingRoot .vcpkg-root)

    $Scripts = Join-Path $TestingRoot "scripts"
    mkdir $Scripts | Out-Null

@"
<?xml version="1.0"?>
<tools version="2">
    <tool name="7zip" os="windows">
        <version>19.00</version>
        <exeRelativePath>Files\7-Zip\7z.exe</exeRelativePath>
        <url>https://www.7-zip.org/a/7z1900-x64.msi</url>
        <sha512>7837a8677a01eed9c3309923f7084bc864063ba214ee169882c5b04a7a8b198ed052c15e981860d9d7952c98f459a4fab87a72fd78e7d0303004dcb86f4324c8</sha512>
        <archiveName>7z1900-x64.msi</archiveName>
    </tool>
    <tool name="ninja-testing" os="windows">
        <version>1.10.2</version>
        <exeRelativePath>ninja.exe</exeRelativePath>
        <url>https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-win.zip</url>
        <sha512>6004140d92e86afbb17b49c49037ccd0786ce238f340f7d0e62b4b0c29ed0d6ad0bab11feda2094ae849c387d70d63504393714ed0a1f4d3a1f155af7a4f1ba3</sha512>
        <archiveName>ninja-win-1.10.2.zip</archiveName>
    </tool>
    <tool name="ninja" os="windows">
        <version>1.10.2</version>
        <exeRelativePath>ninja.exe</exeRelativePath>
        <url>https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-win.zip</url>
        <sha512>6004140d92e86afbb17b49c49037ccd0786ce238f340f7d0e62b4b0c29ed0d6ad0bab11feda2094ae849c387d70d63504393714ed0a1f4d3a1f155af7a4f1ba3</sha512>
        <archiveName>ninja-win-1.10.2.zip</archiveName>
    </tool>
    <tool name="cmake" os="windows">
        <version>3.22.2</version>
        <exeRelativePath>cmake-3.22.2-windows-i386\bin\cmake.exe</exeRelativePath>
        <url>https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-windows-i386.zip</url>
        <sha512>969d3d58d56d8fa3cc3acae2b949bf58abab945f70ae292ff20c9060d845dfc094c613c367a924abff47f307cc33af1467cdb9b75bb857868e38b2c7cdc72f79</sha512>
        <archiveName>cmake-3.22.2-windows-i386.zip</archiveName>
    </tool>
    <tool name="cmake" os="osx">
        <version>3.22.2</version>
        <exeRelativePath>cmake-3.22.2-macos-universal/CMake.app/Contents/bin/cmake</exeRelativePath>
        <url>https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-macos-universal.tar.gz</url>
        <sha512>08104f608ecb9a5cfef38e79f0957d21e425616c0677781445492f82cbfec805113e3b5eb4bc737b707bb26a00678e7bd55e17555a5611c08b0b9b44ac5136ac</sha512>
        <archiveName>cmake-3.22.2-macos-universal.tar.gz</archiveName>
    </tool>
    <tool name="cmake" os="linux">
        <version>3.22.2</version>
        <exeRelativePath>cmake-3.22.2-linux-x86_64/bin/cmake</exeRelativePath>
        <url>https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-linux-x86_64.tar.gz</url>
        <sha512>579e08b086f6903ef063697fca1dc2692f68a7341dd35998990b772b4221cdb5b1deecfa73bad9d46817ef09e58882b2adff9d64f959c01002c11448a878746b</sha512>
        <archiveName>cmake-3.22.2linux-x86_64.tar.gz</archiveName>
    </tool>
    <tool name="cmake" os="freebsd">
        <version>3.20.4</version>
        <exeRelativePath>usr/local/bin/cmake</exeRelativePath>
        <url>https://pkg.freebsd.org/FreeBSD:13:amd64/quarterly/All/cmake-3.20.4.txz</url>
        <sha512>3e5b675d7ff924f92996d912e2365582e687375109ef99c9073fb8196bb329243a406b218cf1358d7cc518988b311ce9e5bf87de4d64f2e6377b7c2bc8894475</sha512>
        <archiveName>cmake-3.20.4.txz</archiveName>
    </tool>
</tools>
"@ | % { $_ -replace "`r","" } | Out-File -enc ascii $(Join-Path $Scripts "vcpkgTools.xml")

    $env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'down loads'
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "7zip", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/7zip-19.00-windows/Files/7-Zip/7z.exe"

    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja-testing", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/ninja-testing-1.10.2-windows/ninja.exe"

    $path = $env:PATH

    $env:PATH = "$path;$TestingRoot/down loads/tools/ninja-testing-1.10.2-windows"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$TestingRoot/down loads/tools/ninja-1.10.2-windows/ninja.exe"

    $env:VCPKG_FORCE_DOWNLOADED_BINARIES = "1"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/ninja-1.10.2-windows/ninja.exe"

    Remove-Item -Recurse -Force "$TestingRoot/down loads/tools/ninja-1.10.2-windows" -ErrorAction SilentlyContinue
    Remove-Item env:VCPKG_FORCE_DOWNLOADED_BINARIES

    $env:VCPKG_FORCE_SYSTEM_BINARIES = "1"
    $env:PATH = "$PSScriptRoot\..\e2e_assets\fetch;$path"
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileNotExists "$TestingRoot/down loads/tools/ninja-1.10.2-windows/ninja.exe"

    Remove-Item env:VCPKG_FORCE_SYSTEM_BINARIES
    $out = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "ninja", "--vcpkg-root=$TestingRoot", "--x-stderr-status"))
    Throw-IfFailed
    & $out --version
    if ($LASTEXITCODE -ne 0) {
        throw 'Couldn''t run resulting ninja'
    }
}
