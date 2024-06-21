param(
    [string]$VcpkgSrcDir = (Join-Path $PSScriptRoot '../..' | Resolve-Path),
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

[scriptblock]$_Gen__ = {
    param(
        [Parameter(Mandatory)][bool]$Expected,
        [Parameter(Mandatory)][string[]]$PackageArray,
        [string]$Baseline = '0' * 40
    )
    return @(
        $Expected,
        @{
            registries = @(
                [pscustomobject]@{
                    kind       = 'git'
                    repository = ''
                    baseline   = $Baseline
                    packages   = $PackageArray
                }
            )
        }
    )
}

# See src/vcpkg-test/registries.cpp "check valid package patterns"
@{
    $VcpkgJsonSchema.Configuration =
    @{
        'packages: ["a"]'                 = & $_Gen__ $true  @('a')
        'packages: empty'                 = & $_Gen__ $false [string[]]@('')
        'packages: blank'                 = & $_Gen__ $false @(' ')
        'packages: baseline 39'           = & $_Gen__ $false @('a')       ('0' * 39)
        'packages: hashtag ["*"]'         = & $_Gen__ $true  @('*')
        'packages: hashtag ["a*"]'        = & $_Gen__ $true  @('a*')
        'packages: hashtag ["*a"]'        = & $_Gen__ $false @('*a')
        'packages: hashtag ["b-*"]'       = & $_Gen__ $true  @('b-*')
        'packages: hashtag ["c-d-*"]'     = & $_Gen__ $true  @('c-d-*')
        'packages: hashtag dup ["a**"]'   = & $_Gen__ $false @('a**')
        'packages: hashtag dup ["b-**"]'  = & $_Gen__ $false @('b-**')
        'packages: hashtag dup ["c--*"]'  = & $_Gen__ $false @('c--*')
        'packages: hashtag dup ["d-*-*"]' = & $_Gen__ $false @('d-*-*')
        'packages: hashtag mid ["a*b"]'   = & $_Gen__ $false @('a*b')
        'packages: mix array ["a*","b"]'  = & $_Gen__ $true  @('a*', 'b')
        'packages: symbols ["a+"]'        = & $_Gen__ $false @('a+')
        'packages: symbols ["a?"]'        = & $_Gen__ $false @('a?')
    }
    $VcpkgJsonSchema.Port          =
    @{
        # test identifiers
        'port-name: "co"'                 = $true, @{name = 'co' }
        'port-name: "rapidjson"'          = $true, @{name = 'rapidjson' }
        'port-name: "boost-tuple"'        = $true, @{name = 'boost-tuple' }
        'port-name: "vcpkg-boost-helper"' = $true, @{name = 'vcpkg-boost-helper' }
        'port-name: "lpt"'                = $true, @{name = 'lpt' }
        'port-name: "com"'                = $true, @{name = 'com' }
        # reject invalid characters
        'port-name: ""'                   = $false, @{name = '' }
        'port-name: " "'                  = $false, @{name = ' ' }
        'port-name: "boost_tuple"'        = $false, @{name = 'boost_tuple' }
        'port-name: "boost.'              = $false, @{name = 'boost.' }
        'port-name: "boost.tuple"'        = $false, @{name = 'boost.tuple' }
        'port-name: "boost@1"'            = $false, @{name = 'boost@1' }
        'port-name: "boost#1"'            = $false, @{name = 'boost#1' }
        'port-name: "boost:x64-windows"'  = $false, @{name = 'boost:x64-windows' }
        # accept legacy
        'port-name: "all_modules"'        = $false, @{name = 'all_modules' } # removed in json-schema
        # reject reserved keywords
        'port-name: "prn"'                = $false, @{name = 'prn' }
        'port-name: "aux"'                = $false, @{name = 'aux' }
        'port-name: "nul"'                = $false, @{name = 'nul' }
        'port-name: "con"'                = $false, @{name = 'con' }
        'port-name: "core"'               = $false, @{name = 'core' }
        'port-name: "default"'            = $false, @{name = 'default' }
        'port-name: "lpt0"'               = $false, @{name = 'lpt0' }
        'port-name: "lpt9"'               = $false, @{name = 'lpt9' }
        'port-name: "com0"'               = $false, @{name = 'com0' }
        'port-name: "com9"'               = $false, @{name = 'com9' }
        # reject incomplete segments
        'port-name: "-a"'                 = $false, @{name = '-a' }
        'port-name: "a-"'                 = $false, @{name = 'a-' }
        'port-name: "a--"'                = $false, @{name = 'a--' }
        'port-name: "---"'                = $false, @{name = '---' }
    }
}.GetEnumerator()
| ForEach-Object {
    @{
        SchemaName = $_.Key
        JsonCases  = $_.Value.GetEnumerator() | ForEach-Object { @{Title = $_.Key; Expected = $_.Value[0]; Json = ConvertTo-Json -InputObject $_.Value[1] -Depth 5 -Compress } }
    }
}
| ForEach-Object {
    $_SchemaName = $_.SchemaName
    $_SchemaPath = Join-Path $TempWorkDir $_SchemaName
    $_.JsonCases | ForEach-Object {
        $_Title = $_.Title
        $_Expected = $_.Expected

        $_Actual = Test-Json -ea:0 -Json $_.Json -SchemaFile $_SchemaPath
        $_Result = $_.Expected -eq $_Actual ? 'Pass':'Fail'
        if ($_Result -eq 'Fail') {
            throw "$_SchemaName validate fail with $_Title, expected $_Expected"
        }
        [pscustomobject]@{
            SchemaName = $_SchemaName
            Title      = $_Title
            Expected   = $_Expected
            Actual     = $_Actual
            Result     = $_Result
        }
    }
}
| Sort-Object SchemaName, Title
| Format-Table

Remove-Item -Recurse -Force $TempWorkDir
