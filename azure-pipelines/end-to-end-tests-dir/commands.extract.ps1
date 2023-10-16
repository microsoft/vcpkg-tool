. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Refresh-TestRoot
$out = Join-Path $TestingRoot "a-tar-with-execute"
Run-Vcpkg z-extract "$PSScriptRoot/../e2e-assets/extract/a-tar-with-plus-x.tar.gz" $out
Throw-IfFailed

$extractedFilePath = Join-Path $out "myExe"
if (-Not (Test-Path $extractedFilePath)) {
    throw "Extraction Failed"
}

if (-Not $IsWindows) {
    $unixMode = (Get-Item $extractedFilePath).UnixMode
    if ($unixMode -ne "-rwxr-xr-x") {
        throw "File does not have +x permission. UnixMode: $unixMode"
    }
}

# Regression test for https://github.com/microsoft/vcpkg/issues/33904 / https://github.com/microsoft/vcpkg-tool/pull/1234
if ($IsWindows) {
    Refresh-TestRoot
    $gitCommand = Get-Command git
    $gitDirectory = (Get-Item $gitCommand.Source).Directory
    $bash = "$gitDirectory/bash.exe"
    if (-Not (Test-Path $bash)) {
        $gitInstallation = $gitDirectory.Parent
        $bash = "$gitInstallation/bin/bash.exe"
        if (-Not (Test-Path $bash)) {
            throw 'git bash not found'
        }
    }

    $out = Join-Path $TestingRoot "a-tar-with-execute"
    [string]$vcpkgExeForwardSlashes = $vcpkgExe.Replace("\", "/")
    [string]$tarballForwardSlashes = "$PSScriptRoot/../e2e-assets/extract/a-tar-with-plus-x.tar.gz".Replace("\", "/")
    [string]$outForwardSlashes = $out.Replace("\", "/")
    & $bash -c "`"$vcpkgExeForwardSlashes`" z-extract `"$tarballForwardSlashes`" `"$outForwardSlashes`" --debug"
    $extractedFilePath = Join-Path $out "myExe"
    if (-Not (Test-Path $extractedFilePath)) {
        throw "Extraction Failed"
    }
}
