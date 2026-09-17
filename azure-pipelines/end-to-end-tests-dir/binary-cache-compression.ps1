. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @("--overlay-ports=$PSScriptRoot/../e2e-ports", "--host-triplet=$Triplet")
$oldLevel = $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL
try {
    foreach ($level in @(0, 1, 5, 9)) {
        Refresh-TestRoot
        # Exercise the environment setting and command-line precedence over an invalid environment value.
        $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = "$level"
        $levelArgs = @()
        if ($level -ne 1) {
            $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = 'invalid'
            $levelArgs = @("--binary-cache-compression-level=$level")
        }
        Run-Vcpkg -TestArgs ($commonArgs + $levelArgs + @('install', 'binary-cache-compression', "--binarysource=clear;files,$ArchiveRoot,write"))
        Throw-IfFailed

        $archives = @(Get-ChildItem $ArchiveRoot -Recurse -Filter '*.zip')
        if ($archives.Count -ne 1) { throw "Expected one binary cache archive" }
        $archive = [System.IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
        try {
            $entry = $archive.GetEntry('share/binary-cache-compression/copyright')
            if ($null -eq $entry) { throw 'Missing package content in archive' }
            if ($level -eq 0) {
                if ($entry.CompressedLength -ne $entry.Length) { throw 'Level 0 must store without compression' }
            } elseif ($entry.CompressedLength -ge $entry.Length) {
                throw 'Expected compression of repetitive package content'
            }
        } finally {
            $archive.Dispose()
        }

        Remove-Item -Recurse -Force $installRoot
        Remove-Item -Recurse -Force $buildtreesRoot
        # A different compression setting must still restore the same ABI from the cache.
        $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = '9'
        $output = Run-VcpkgAndCaptureOutput ($commonArgs + @('install', 'binary-cache-compression', "--binarysource=clear;files,$ArchiveRoot,read"))
        Throw-IfFailed
        Require-FileExists "$installRoot/$Triplet/share/binary-cache-compression/copyright"
        Throw-IfNonContains -Actual $output -Expected "Restored 1 package(s)"
        Require-FileNotExists "$buildtreesRoot/binary-cache-compression/built-marker"
    }
} finally {
    $env:VCPKG_BINARY_CACHE_COMPRESSION_LEVEL = $oldLevel
}
