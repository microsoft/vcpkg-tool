param(
    [string]$VcpkgSrcDir = (Join-Path $PSScriptRoot '../..' | Resolve-Path),
    [string]$VcpkgRegistryDir = $env:VCPKG_ROOT,
    [string]$TempWorkDir = (New-Item -ItemType Directory Temp:/$(New-Guid)).FullName
)
$ErrorActionPreference = 'Stop'
# . $PSScriptRoot/../end-to-end-tests-prelude.ps1

$VcpkgJsonSchema = @{
    Artifact      = 'artifact.schema.json'
    Configuration = 'vcpkg-configuration.schema.json'
    Definitions   = 'vcpkg-schema-definitions.schema.json'
    Port          = 'vcpkg.schema.json'
}

# remove `$id` in schema for error 'Test-Json: Cannot parse the JSON schema.'
$VcpkgJsonSchema.Values
| ForEach-Object {
    Copy-Item -Path (Join-path $VcpkgSrcDir 'docs' $_) -Destination $TempWorkDir
    Join-Path $TempWorkDir $_
}
| ForEach-Object {
    (Get-Content -Raw -Path $_) -replace '(?s)\n  "\$id".+?\n', "`n"
    | Set-Content -NoNewline -Path $_
}
| Out-Null

$VcpkgPortSchemaPath = Join-Path $TempWorkDir $VcpkgJsonSchema.Port

Get-ChildItem -Directory -Path (Join-Path $VcpkgRegistryDir 'ports')
| ForEach-Object -Parallel {
    $PortName = $_.Name
    $PortDir = $_.FullName
    $PortJsonPath = Join-Path $PortDir 'vcpkg.json'
    $Schema = $using:VcpkgPortSchemaPath

    $_Actual = Test-Json -ea:0 -LiteralPath $PortJsonPath -SchemaFile $Schema
    [pscustomobject]@{
        PortName = $PortName
        Actual   = $_Actual
    }
}
| ForEach-Object { Write-Host $_; $_ }
| Where-Object Actual -EQ $false
| ForEach-Object { Write-Error $_; $_ }

Remove-Item -Recurse -Force $TempWorkDir
