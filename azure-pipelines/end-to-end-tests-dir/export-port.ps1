. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$testOutput = "$TestingRoot/exported-ports"
$testPorts = "$PSScriptRoot/../e2e_ports/version-files/ports"
$testVersionsDb = "$PSScriptRoot/../e2e_ports/version-files/versions"

Write-Host "[x-export-port] Export local port files"
Run-Vcpkg -EndToEndTestSilent x-export-port cat "$testOutput" `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/cat" "$testOutput/cat"
if ($diff) {
    Refresh-TestRoot
    Write-Output $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}
# end "[x-export-port] Export local port files"

Write-Host "[x-export-port] Refuse to export to non-empty directory"
New-Item -ItemType "directory" -Path "$testOutput/dog" | Out-Null
Out-File -FilePath "$testOutput/dog/HelloWorld.txt" | Out-Null
Run-Vcpkg -EndToEndTestSilent x-export-port dog "$testOutput" `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfNotFailed
# end "[x-export-port] Refuse to export to non-empty directory"

Write-Host "[x-export-port] Force export to non-empty directory"
Refresh-TestRoot
New-Item -ItemType "directory" -Path "$testOutput/dog" | Out-Null
Out-File -FilePath "$testOutput/dog/HelloWorld.txt" | Out-Null
Run-Vcpkg -EndToEndTestSilent x-export-port dog "$testOutput" --force `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/dog" "$testOutput/dog"
if ($diff) {
    Refresh-TestRoot
    Write-Output $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# end "[x-export-port] Force export to non-empty directory"

Write-Host "[x-export-port] Don't create package sub-directory"
Run-Vcpkg -EndToEndTestSilent x-export-port mouse "$testOutput/mickey" --no-subdir `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/mouse" "$testOutput/mickey"
if ($diff) {
    Refresh-TestRoot
    Write-Output $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}
# end "[x-export-port] Don't create package sub-directory"

Write-Host "[x-export-port] Export version port files"
Run-Vcpkg -EndToEndTestSilent x-export-port duck mallard "$testOutput" `
    --x-builtin-ports-root="$testPorts" `
    --x-builtin-registry-versions-dir="$testVersionsDb" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/duck" "$testOutput/duck"
if ($diff) {
    Refresh-TestRoot
    Write-Output $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}
# end "[x-export-port] Export version port files"

Write-Host "[x-export-port] Add version suffix to destination"
Run-Vcpkg -EndToEndTestSilent x-export-port duck mallard "$testOutput" --add-version-suffix `
    --x-builtin-ports-root="$testPorts" `
    --x-builtin-registry-versions-dir="$testVersionsDb" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/duck" "$testOutput/duck-mallard"
if ($diff) {
    Refresh-TestRoot
    Write-Output $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}
# "[x-export-port] Add version suffix to destination"

Refresh-TestRoot
