. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$publicCommands = @(
    'install'
    'search'
    'remove'
    'list'
    'update'
    'hash'
    'help'
    'integrate'
    'export'
    'edit'
    'create'
    'owns'
    'cache'
    'version'
    'contact'
    'upgrade'
)

$privateCommands = @(
    'build'
    'buildexternal'
    'ci'
    'depend-info'
    'env'
    'portsdiff'
)

function getOptionsForPrefix($prefix, $commands) {
    ($commands | Sort-Object | ? { $_.StartsWith($prefix) }) -join '`n'
}
function arraysEqual($arr1, $arr2) {
    if ($arr1.Length -ne $arr2.Length) {
        $False
    } else {
        for ($i = 0; $i -lt $arr1.Length; ++$i) {
            if ($arr1[$i] -ne $arr2[$i]) {
                return $False
            }
        }
    }
    $True
}

$publicPrefixesToTest = @(
    'in'
    's'
    'rem'
    'h'
    'upgra'
    'e'
)
$privatePrefixesToTest = @(
    'b'
    'build'
    'buildext'
    'ci'
    'dep'
    'en'
    'port'
    'notprefix'
)

function testPrefixes($toTest, $commands) {
    $toTest | % {
        $expected = getOptionsForPrefix $_ $commands
        $found = Run-Vcpkg autocomplete $_
        Throw-IfFailed
        if (-not $expected -eq $found) {
            Write-Host "unexpected result from vcpkg autocomplete:
        expected: `"$($expected -join '" "')`"
        found   : `"$($found -join '" "')`""
            throw
        }
    }
}

testPrefixes $publicPrefixesToTest $publicCommands
testPrefixes $privatePrefixesToTest $privateCommands

# autocomplete is currently broken, so we only test the parts that are correct
