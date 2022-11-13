. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

$testOutput = "$TestingRoot/exported-ports"
$testPorts = "$PSScriptRoot/../e2e_ports/version-files/ports"
$testVersionsDb = "$PSScriptRoot/../e2e_ports/version-files/versions"

$CurrentTest = "[x-export-port] Export local port files"
Run-Vcpkg x-export-port cat "$testOutput" `
    "--x-builtin-ports-root=$testPorts"
Throw-IfFailed

$diff = git diff --no-index -- "$testPorts/cat" "$testOutput/cat"
if ($diff) {
    throw "Exported files don't match the source"
}
# end "[x-export-port] Export local port files"

$CurrentTest = "[x-export-port] Refuse to export to non-empty directory"
New-Item -ItemType "directory" -Path "$testOutput/dog"
Out-File -FilePath "$testOutput/dog/HelloWorld.txt"
Run-Vcpkg x-export-port dog "$testOutput" `
      "--x-builtin-ports-root=$testPorts"
Throw-IfNotFailed
# end "[x-export-port] Refuse to export to non-empty directory"

$CurrentTest = "[x-export-port] Force export to non-empty directory"
Run-Vcpkg x-export-port dog "$testOutput" --force `
      "--x-builtin-ports-root=$testPorts"
Throw-IfFailed

$diff = git diff --no-index -- "$testPorts/dog" "$testOutput/dog"
if ($diff) {
    throw "Exported files don't match the source"
}
# end "[x-export-port] Force export to non-empty directory"

$CurrentTest = "[x-export-port] Don't create package sub-directory"
Run-Vcpkg x-export-port mouse "$testOutput/mickey" --no-subdir `
      "--x-builtin-ports-root=$testPorts"
Throw-IfFailed

$diff = git diff --no-index -- "$testPorts/mouse" "$testOutput/mickey"
if ($diff) {
    throw "Exported files don't match the source"
}
# end "[x-export-port] Don't create package sub-directory"

$CurrentTest = "[x-export-port] Export version port files"
Run-Vcpkg x-export-port duck mallard "$testOutput" `
      "--x-builtin-ports-root=$testPorts" `
      "--x-builtin-registry-versions-dir=$testVersionsDb"
Throw-IfFailed

$diff = git diff --no-index -- "$testPorts/duck" "$testOutput/duck"
if ($diff) {
    throw "Exported files don't match the source"
}
# end "[x-export-port] Export version port files"

$CurrentTest = "[x-export-port] Add version suffix to destination"
Run-Vcpkg x-export-port duck mallard "$testOutput" --add-version-suffix `
      "--x-builtin-ports-root=$testPorts" `
      "--x-builtin-registry-versions-dir=$testVersionsDb"
Throw-IfFailed

$diff = git diff --no-index -- "$testPorts/duck" "$testOutput/duck-mallard"
if ($diff) {
    throw "Exported files don't match the source"
}
# "[x-export-port] Add version suffix to destination"

Refresh-TestRoot
