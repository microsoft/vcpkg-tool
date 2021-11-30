[CmdletBinding()]
Param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,
    [Parameter(Mandatory = $true)]
    [string]$VcpkgBaseVersion
)

$versionUnlockedUri = "https://github.com/microsoft/vcpkg-tool/releases/latest/download/"
$versionLockedUri = "https://github.com/microsoft/vcpkg-tool/releases/download/$VcpkgBaseVersion/"
$pwshInstaller = Get-Content "$PSScriptRoot\vcpkg-init.ps1" -Raw
$pwshInstaller = $pwshInstaller.Replace($versionUnlockedUri, $versionLockedUri)
Set-Content -Path "$Destination\vcpkg-init.ps1" -Value $pwshInstaller
