
$VcpkgPortSchemaPath = Join-Path $WorkingRoot $VcpkgJsonSchema.Port

Get-ChildItem -Directory -Path (Join-Path $VcpkgRoot 'ports')
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
