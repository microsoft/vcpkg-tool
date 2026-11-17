. $PSScriptRoot/../end-to-end-tests-prelude.ps1


$manifestPath = "$PSScriptRoot/../e2e-projects/export-project"
$outputDir = "$manifestPath/output"
Run-Vcpkg install --x-manifest-root=$manifestPath
Throw-IfFailed

Run-Vcpkg export --zip --x-manifest-root=$manifestPath --output-dir=$outputDir
Throw-IfFailed

Run-Vcpkg export --nuget --x-manifest-root=$manifestPath --output-dir=$outputDir
Throw-IfFailed

Run-Vcpkg export --7zip --x-manifest-root=$manifestPath --output-dir=$outputDir
Throw-IfFailed

# Check existence of zip file(s)
$zipFilesExist = Test-Path "$outputDir/*.zip"
if (-Not $zipFilesExist)
{
    throw "No zip files found in $outputDir"
}

# Check contents of NuGet file(s)
$nuGetFiles = Get-ChildItem "$outputDir/*.nupkg"
if ($nuGetFiles.Count -eq 0)
{
	throw "No NuGet files found in $outputDir"
}

foreach ($nuGetFile in $nuGetFiles)
{
    $archive = [System.IO.Compression.ZipFile]::OpenRead($nuGetFile)
    try
    {
        $entryNames = $archive.Entries.FullName
        $packageId = $nuGetFile.BaseName -replace "\.1\.0\.0$", ""
        $expectedFiles = @(
            ".vcpkg-root",
            "build/native/$packageId.props",
            "build/native/$packageId.targets"
        )
        if ($IsWindows)
        {
            $expectedFiles += "vcpkg.exe"
        }

        foreach ($expectedFile in $expectedFiles)
        {
            if ($entryNames -notcontains $expectedFile)
            {
                throw "$nuGetFile does not contain $expectedFile"
            }
        }

        $redirects = @{
            "build/native/$packageId.props" = "../../scripts/buildsystems/msbuild/vcpkg.props"
            "build/native/$packageId.targets" = "../../scripts/buildsystems/msbuild/vcpkg.targets"
        }
        foreach ($redirect in $redirects.GetEnumerator())
        {
            $reader = [System.IO.StreamReader]::new($archive.GetEntry($redirect.Key).Open())
            try
            {
                if ($reader.ReadToEnd() -notlike "*$($redirect.Value)*")
                {
                    throw "$nuGetFile contains an invalid $($redirect.Key)"
                }
            }
            finally
            {
                $reader.Dispose()
            }
        }
    }
    finally
    {
        $archive.Dispose()
    }
}

# Check existence of 7zip file(s)
$sevenZipFilesExist = Test-Path "$outputDir/*.7z"
if (-Not $sevenZipFilesExist)
{
	throw "No 7zip files found in $outputDir"
}

# Cleanup exported packages
Get-ChildItem -Path $manifestPath | Where-Object { $_.Name -ne "vcpkg.json" -and $_.Name -ne "vcpkg_installed" } | Remove-Item -Recurse -Force

# Test export with invalid <port:triplet> argument
$out = Run-VcpkgAndCaptureOutput export zlib:x64-windows --zip --x-manifest-root=$manifestPath --output-dir=$manifestPath
Throw-IfNotFailed
if ($out -notmatch "unexpected argument: zlib:x64-windows")
{
    throw "Expected to fail and print warning about unexpected argument"
}

# Test export with missing --output-dir argument
$out = Run-VcpkgAndCaptureOutput export --zip --x-manifest-root=$manifestPath
Throw-IfNotFailed
if ($out -notmatch "This command requires --output-dir")
{
	throw "Expected to fail and print warning about missing argument"
}

# Test export with empty export plan
Remove-Item -Path "$manifestPath/vcpkg_installed" -Recurse -Force
$out = Run-VcpkgAndCaptureOutput export --zip --x-manifest-root=$manifestPath --output-dir=$manifestPath
Throw-IfNotFailed
if ($out -notmatch "Refusing to create an export of zero packages. Install packages before exporting.")
{
	throw "Expected to fail and print warning about empty export plan."
}
