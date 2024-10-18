
Write-Host "It's spec."

if ($IsLinux) {
    throw 'It is error test for end-to-end-tests.ps1 only on Linux'
}
