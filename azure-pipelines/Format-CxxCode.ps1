#! /usr/bin/env pwsh

[CmdletBinding()]
$Root = Resolve-Path -LiteralPath "$PSScriptRoot/.."

$clangFormat = Get-Command 'clang-format-14' -ErrorAction 'SilentlyContinue'
if ($null -eq $clangFormat)
{
    $clangFormat = Get-Command 'clang-format' -ErrorAction 'SilentlyContinue'
}

if ($null -ne $clangFormat)
{
    $clangFormat = $clangFormat.Source
}

if ($IsWindows)
{
    if ([String]::IsNullOrEmpty($clangFormat) -or -not (Test-Path $clangFormat))
    {
        $clangFormat = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe'
    }
    if ([String]::IsNullOrEmpty($clangFormat) -or -not (Test-Path $clangFormat))
    {
        $clangFormat = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe'
    }
    if (-not (Test-Path $clangFormat))
    {
        $clangFormat = 'C:\Program Files\LLVM\bin\clang-format.exe'
    }
}

if ([String]::IsNullOrEmpty($clangFormat) -or -not (Test-Path $clangFormat))
{
    Write-Error 'clang-format not found; is it installed?'
    throw
}

$clangFormatVersion = "$(& $clangFormat --version)".Trim()
"Using $clangFormatVersion located at $clangFormat" | Write-Host

$files = Get-ChildItem -Recurse -LiteralPath "$Root/src" -Filter '*.cpp'
$files += Get-ChildItem -Recurse -LiteralPath "$Root/src" -Filter '*.c'
$files += Get-ChildItem -Recurse -LiteralPath "$Root/include/vcpkg" -Filter '*.h'
$files += Get-ChildItem -Recurse -LiteralPath "$Root/include/vcpkg-test" -Filter '*.h'
$files += Get-Item "$Root/include/pch.h"
$fileNames = $files.FullName

& $clangFormat -style=file -i @fileNames
