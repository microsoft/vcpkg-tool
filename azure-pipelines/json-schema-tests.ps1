[CmdletBinding()]
Param(
    [ValidateNotNullOrEmpty()]
    [string]$WorkingRoot = 'work',
    [Parameter(Mandatory = $false)]
    [string]$VcpkgRoot
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -lt 7) {
    Write-Error "json-schema e2e tests must use pwsh"
}

$VcpkgSrcDir = $PWD

$WorkingRoot = (New-Item -Path $WorkingRoot -ItemType Directory -Force).FullName

$VcpkgRoot = & {
    if (-not [string]::IsNullOrWhitespace($VcpkgRoot)) {
        return $VcpkgRoot
    }
    if ([string]::IsNullOrWhitespace($env:VCPKG_ROOT)) {
        throw "Could not determine VCPKG_ROOT"
    }
    return $env:VCPKG_ROOT
} | Get-Item | Select-Object -ExpandProperty FullName

$VcpkgJsonSchema = @{
    Artifact      = 'artifact.schema.json'
    Configuration = 'vcpkg-configuration.schema.json'
    Definitions   = 'vcpkg-schema-definitions.schema.json'
    Port          = 'vcpkg.schema.json'
}

# remove `$id` in schema for error 'Test-Json: Cannot parse the JSON schema.'
$VcpkgJsonSchema.Values
| ForEach-Object {
    Copy-Item -Path (Join-path $VcpkgSrcDir 'docs' $_) -Destination $WorkingRoot
    Join-Path $WorkingRoot $_
}
| ForEach-Object {
    (Get-Content -Raw -Path $_) -replace '(?s)\n  "\$id".+?\n', "`n"
    | Set-Content -NoNewline -Path $_
}
| Out-Null

Get-ChildItem $PSScriptRoot/json-schema-tests-dir/*.test.ps1
| ForEach-Object {
    Write-Host "Running test $_"
    & $_.FullName
}
