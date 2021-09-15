. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Not a number
Run-Vcpkg ci $Triplet --x-skipped-cascade-count=fish
Throw-IfNotFailed

# Negative
Run-Vcpkg ci $Triplet --x-skipped-cascade-count=-1
Throw-IfNotFailed

# Clearly not the correct answer
Run-Vcpkg ci $Triplet --x-skipped-cascade-count=1000
Throw-IfNotFailed
