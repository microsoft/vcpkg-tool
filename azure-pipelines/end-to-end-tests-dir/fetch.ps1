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
</tools>
"@ | % { $_ -replace "`r","" } | Out-File -enc ascii $(Join-Path $Scripts "vcpkgTools.xml")

    $env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'down loads'
    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "7zip", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/7zip-19.00-windows/Files/7-Zip/7z.exe"

    Run-Vcpkg -TestArgs ($commonArgs + @("fetch", "ninja-testing", "--vcpkg-root=$TestingRoot"))
    Throw-IfFailed
    Require-FileExists "$TestingRoot/down loads/tools/ninja-testing-1.10.2-windows/ninja.exe"
}
