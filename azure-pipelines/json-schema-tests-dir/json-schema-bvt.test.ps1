
function GenPackageJson {
    param(
        [Parameter(Mandatory)][bool]$Expected,
        [Parameter(Mandatory)][AllowEmptyString()][string[]]$PackageArray,
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
        'packages: ["a"]'                 = GenPackageJson $true  @('a')
        'packages: empty'                 = GenPackageJson $false [string[]]@('')
        'packages: blank'                 = GenPackageJson $false @(' ')
        'packages: baseline 39'           = GenPackageJson $false @('a') ('0' * 39)
        'packages: hashtag ["*"]'         = GenPackageJson $true  @('*')
        'packages: hashtag ["a*"]'        = GenPackageJson $true  @('a*')
        'packages: hashtag ["*a"]'        = GenPackageJson $false @('*a')
        'packages: hashtag ["b-*"]'       = GenPackageJson $true  @('b-*')
        'packages: hashtag ["c-d-*"]'     = GenPackageJson $true  @('c-d-*')
        'packages: hashtag dup ["a**"]'   = GenPackageJson $false @('a**')
        'packages: hashtag dup ["b-**"]'  = GenPackageJson $false @('b-**')
        'packages: hashtag dup ["c--*"]'  = GenPackageJson $false @('c--*')
        'packages: hashtag dup ["d-*-*"]' = GenPackageJson $false @('d-*-*')
        'packages: hashtag mid ["a*b"]'   = GenPackageJson $false @('a*b')
        'packages: mix array ["a*","b"]'  = GenPackageJson $true  @('a*', 'b')
        'packages: symbols ["a+"]'        = GenPackageJson $false @('a+')
        'packages: symbols ["a?"]'        = GenPackageJson $false @('a?')
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
        JsonCases  = $_.Value.GetEnumerator() | ForEach-Object {
            @{
                Title    = $_.Key
                Expected = $_.Value[0]
                Json     = ConvertTo-Json -InputObject $_.Value[1] -Depth 5 -Compress
            }
        }
    }
}
| ForEach-Object {
    $_SchemaName = $_.SchemaName
    $_SchemaPath = Join-Path $WorkingRoot $_SchemaName
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
