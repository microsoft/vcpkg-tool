. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$out = Join-Path $TestingRoot "a-tar-with-execute"
Run-Vcpkg z-extract "$PSScriptRoot/../e2e-assets/extract/a-tar-with-plus-x.tar.gz" $out
Throw-IfFailed

if (-Not $IsWindows) {
    $extractedFilePath = Join-Path $out "myExe"
    if ((Get-Item $extractedFilePath).UnixMode -notmatch ".*x.*") {
        throw "File does not have +x permission"
    }
}