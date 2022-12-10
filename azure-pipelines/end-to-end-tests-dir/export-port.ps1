. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$testOutput = "$TestingRoot/exported-ports"
$testPorts = "$PSScriptRoot/../e2e_ports/version-files/ports"
$testVersionsDb = "$PSScriptRoot/../e2e_ports/version-files/versions"

# [x-export-port] Export local port files
Run-Vcpkg -EndToEndTestSilent x-export-port cat "$testOutput/cat" `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/cat" "$testOutput/cat"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export local port files"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Fail local port file not exists
Run-Vcpkg -EndToEndTestSilent x-export-port unicorn "${testOutput}/unicorn" `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfNotFailed

# [x-export-port] Refuse to export to non-empty directory
Refresh-TestRoot
New-Item -ItemType "directory" -Path "$testOutput/dog" | Out-Null
Out-File -FilePath "$testOutput/dog/HelloWorld.txt" | Out-Null
Run-Vcpkg -EndToEndTestSilent x-export-port dog "$testOutput/dog" `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfNotFailed -Message "[x-export-port] Refuse to export to non-empty directory"

# [x-export-port] Force export to non-empty directory
Refresh-TestRoot
New-Item -ItemType "directory" -Path "$testOutput/dog" | Out-Null
Out-File -FilePath "$testOutput/dog/HelloWorld.txt" | Out-Null
Run-Vcpkg -EndToEndTestSilent x-export-port dog "$testOutput/dog" --force `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/dog" "$testOutput/dog"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Force export to non-empty directory"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Don't create package sub-directory
Refresh-TestRoot
Run-Vcpkg -EndToEndTestSilent x-export-port mouse "$testOutput" --subdir `
    --x-builtin-ports-root="$testPorts" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/mouse" "$testOutput/mouse"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Don't create package sub-directory"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Export version port files
Refresh-TestRoot
Run-Vcpkg -EndToEndTestSilent x-export-port duck@mallard "$testOutput/duck" `
    --x-builtin-ports-root="$testPorts" `
    --x-builtin-registry-versions-dir="$testVersionsDb" `
    | Out-Null
Throw-IfFailed
$diff = git diff --no-index -- "$testPorts/duck" "$testOutput/duck"
if ($diff) {
    Refresh-TestRoot
    Write-Trace "error: [x-export-port] Export version port files"
    Write-Host $Script::CurrentTest
    $diff
    throw "exported files don't match the source"
}

# [x-export-port] Fail if no destination
Run-Vcpkg -EndToEndTestSilent x-export-port duck@mallard `
    --x-builtin-ports-root="$testPorts" `
    --x-builtin-registry-versions-dir="$testVersionsDb" `
    | Out-Null
Throw-IfNotFailed
