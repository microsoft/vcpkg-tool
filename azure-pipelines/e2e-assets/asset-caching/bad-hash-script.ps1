Param([string]$File)
Write-Host "Creating file with the wrong hash"
Set-Content -Path $File -Value "This is a file with the wrong hash" -Encoding Ascii -NoNewline