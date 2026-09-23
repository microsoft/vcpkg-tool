. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @("--overlay-ports=$PSScriptRoot/../e2e-ports", "--host-triplet=$Triplet")
$oldLevel = $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL
try {
    foreach ($level in (@($null) + @(0..9))) {
        Refresh-TestRoot
        # Exercise the tool default, the environment setting, and command-line precedence.
        Remove-Item Env:\VCPKG_BINARY_CACHE_COMPRESSION_LEVEL -ErrorAction SilentlyContinue
        $levelArgs = @()
        if ($level -eq 1) {
            $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = "$level"
        } elseif ($null -ne $level) {
            $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = 'invalid'
            $levelArgs = @("--binary-cache-compression-level=$level")
        }
        Run-Vcpkg -TestArgs ($commonArgs + $levelArgs + @('install', 'vcpkg-header-only', "--binarysource=clear;files,$ArchiveRoot,write"))
        Throw-IfFailed

        $archives = @(Get-ChildItem $ArchiveRoot -Recurse -Filter '*.zip')
        if ($archives.Count -ne 1) { throw "Expected one binary cache archive" }
        $archive = [System.IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
        try {
            $entry = $archive.GetEntry('include/vcpkg-header-only.h')
            if ($null -eq $entry) { throw 'Missing package content in archive' }
            if ($level -eq 0) {
                foreach ($entry in $archive.Entries) {
                    if ($entry.CompressedLength -ne $entry.Length) { throw 'Level 0 must store without compression' }
                }
            } elseif ($null -ne $level) {
                # Tiny files may grow under compression; check that at least one entry shrinks.
                $compressedEntries = @($archive.Entries | Where-Object { $_.CompressedLength -lt $_.Length })
                if ($compressedEntries.Count -eq 0) { throw 'Expected compressed package content' }
            }
        } finally {
            $archive.Dispose()
        }
    }

    # Restore once with a different setting; rebuilding must not hide a cache miss.
    $payloadPath = "$installRoot/$Triplet/include/vcpkg-header-only.h"
    $originalHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    Remove-Item -Recurse -Force $installRoot
    $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = '0'
    Run-Vcpkg -TestArgs ($commonArgs + @('install', 'vcpkg-header-only', '--only-binarycaching', "--binarysource=clear;files,$ArchiveRoot,read"))
    Throw-IfFailed
    $restoredHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    if ($restoredHash -ne $originalHash) {
        throw 'Restored package content does not match the original payload'
    }
} finally {
    $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = $oldLevel
}
