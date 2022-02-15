[CmdletBinding()]
Param()

$BasePath = Get-Item "$PSScriptRoot/../.."

[String[]]$fileList = Get-Content -LiteralPath $Env:XLocFileList
$fileList | % {
    $fileName = Get-Item "$BasePath/$_"
    $fileContents = Get-Content $fileName -Raw # removes the BOM
    $fileContents += "`n" # add a trailing newline
    $fileContents | Out-File `
        -FilePath $fileName `
        -Encoding UTF8NoBOM `
        -NoNewline
}
