[CmdletBinding()]
Param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,
    [Parameter(Mandatory = $true)]
    [string]$VcpkgBaseVersion
)

$versionUnlockedUri = "https://github.com/microsoft/vcpkg-tool/releases/latest/download/"
$versionLockedUri = "https://github.com/microsoft/vcpkg-tool/releases/download/$VcpkgBaseVersion/"

$pwshInstaller = Get-Content "$PSScriptRoot\vcpkg-init.ps1" -Raw -Encoding ascii
$pwshInstaller = $pwshInstaller.Replace($versionUnlockedUri, $versionLockedUri)
Set-Content -Path "$Destination\vcpkg-init.ps1" -Value $pwshInstaller -NoNewline -Encoding ascii

$shInstaller = Get-Content "$PSScriptRoot\vcpkg-init" -Raw -Encoding ascii
$shInstaller = $shInstaller.Replace($versionUnlockedUri, $versionLockedUri)
Set-Content -Path "$Destination\vcpkg-init" -Value $shInstaller -NoNewline -Encoding ascii
