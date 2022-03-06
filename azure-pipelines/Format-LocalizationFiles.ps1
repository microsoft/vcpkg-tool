#! /usr/bin/env pwsh

[CmdletBinding()]
$Root = Resolve-Path -LiteralPath "$PSScriptRoot/.."

Get-ChildItem "$Root/locales/messages.*.json" | ? {
  (Split-Path -Leaf $_) -ne 'messages.en.json'
} | % {
    $fileName = $_
    Write-Host "Formatting $fileName"
    $fileContents = Get-Content $fileName
    $fileContents = $fileContents -join "`n" # use LF, not CRLF
    if (-not $fileContents.EndsWith("`n"))
    {
        $fileContents += "`n" # add a trailing newline
    }
    # this (convert to UTF-8 followed by WriteAllBytes) avoids adding a BOM in Windows PowerShell
    $fileContents = [System.Text.Encoding]::UTF8.GetBytes($fileContents)
    [io.file]::WriteAllBytes($fileName, $fileContents)
}
