# This was written by GPT-5.1-Codex-Max to generate the table in vcpkg release notes.

param(
    [Parameter(Position = 0)]
    [string]$Path,
    [string]$BuildUri
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Normalize-TripletName {
    param([string]$Name)
    return ($Name -replace '_', '-')
}

$cleanupPath = $null
$downloadPath = $null
try {
    if (-not $Path -and -not $BuildUri) {
        throw "Specify either -Path or -BuildUri."
    }

    if ($Path -and $BuildUri) {
        throw "Specify only one of -Path or -BuildUri."
    }

    if ($BuildUri) {
        if ($BuildUri -match 'buildId=([0-9]+)') {
            $buildId = $matches[1]
        }
        elseif ($BuildUri -match '/builds/([0-9]+)/') {
            $buildId = $matches[1]
        }
        else {
            throw "Could not determine buildId from BuildUri '$BuildUri'."
        }

        if ($BuildUri -match '/logs\?\$format=zip$') {
            $downloadUri = $BuildUri
        }
        else {
            $downloadUri = "https://dev.azure.com/vcpkg/c1ee48cb-0df2-4ab3-8384-b1df5a79fe53/_apis/build/builds/{0}/logs?$format=zip" -f $buildId
        }

        $downloadPath = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ("build_logs_{0}_{1}.zip" -f $buildId, [guid]::NewGuid())
        Invoke-WebRequest -Uri $downloadUri -OutFile $downloadPath -Headers @{ Accept = 'application/zip' }

        $sig = Get-Content -LiteralPath $downloadPath -AsByteStream -TotalCount 2
        if (-not ($sig.Length -eq 2 -and $sig[0] -eq 0x50 -and $sig[1] -eq 0x4B)) {
            throw "Downloaded file is not a zip. URL: $downloadUri"
        }
        $Path = $downloadPath
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Path '$Path' does not exist."
    }

    $rootPath = (Resolve-Path -LiteralPath $Path).Path
    if ((Test-Path -LiteralPath $rootPath -PathType Leaf) -and ([IO.Path]::GetExtension($rootPath) -ieq '.zip')) {
        $cleanupPath = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ("logs_extract_{0}" -f [guid]::NewGuid())
        New-Item -ItemType Directory -Path $cleanupPath | Out-Null
        Expand-Archive -LiteralPath $rootPath -DestinationPath $cleanupPath -Force
        $rootPath = $cleanupPath
    }

    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
        throw "Path '$Path' is not a directory or zip archive."
    }

    $wrapperFiles = Get-ChildItem -LiteralPath $rootPath -File -Filter "*_ Test Modified Ports.txt"
    $wrapperDirs = Get-ChildItem -LiteralPath $rootPath -Directory
    if (-not $wrapperFiles -and $wrapperDirs.Count -eq 1) {
        $rootPath = $wrapperDirs[0].FullName
    }

    $results = @()
    foreach ($dir in Get-ChildItem -LiteralPath $rootPath -Directory) {
        if ($dir.Name -ieq 'Agent Diagnostic Logs') { continue }

        $tripletName = Normalize-TripletName -Name $dir.Name
        $logFile = Get-ChildItem -LiteralPath $dir.FullName -File -Filter "*_ Test Modified Ports.txt" | Sort-Object Name | Select-Object -First 1
        if (-not $logFile) { continue }

        $succeeded = $null
        $inTripletSummary = $false
        foreach ($line in Get-Content -LiteralPath $logFile.FullName) {
            if ($line -match 'SUMMARY FOR\s+([^\s]+)') {
                $inTripletSummary = ($matches[1].Trim() -eq $tripletName)
                continue
            }

            if ($inTripletSummary -and $line -match 'SUCCEEDED:\s+(\d+)') {
                $succeeded = [int]$matches[1]
                break
            }
        }

        if ($null -ne $succeeded) {
            $results += [PSCustomObject]@{
                Triplet  = $tripletName
                Succeeded = $succeeded
            }
        }
    }

    $desiredOrder = @(
        'x86-windows',
        'x64-windows',
        'x64-windows-release',
        'x64-windows-static',
        'x64-windows-static-md',
        'x64-uwp',
        'arm64-windows',
        'arm64-windows-static-md',
        'arm64-uwp',
        'arm64-osx',
        'x64-linux',
        'arm-neon-android',
        'x64-android',
        'arm64-android'
    )

    $boldTriplets = @('x64-windows', 'arm64-osx', 'x64-linux')

    $orderedResults = @()
    foreach ($name in $desiredOrder) {
        $match = $results | Where-Object { $_.Triplet -eq $name }
        if ($match) { $orderedResults += $match }
    }

    $orderedResults += $results | Where-Object { $_.Triplet -notin $desiredOrder } | Sort-Object Triplet

    Write-Output '|triplet|ports available|'
    Write-Output '|---|---|'
    foreach ($result in $orderedResults) {
        $tripletLabel = if ($boldTriplets -contains $result.Triplet) { "**$($result.Triplet)**" } else { $result.Triplet }
        Write-Output ("|{0}|{1}|" -f $tripletLabel, $result.Succeeded)
    }
}
finally {
    if ($cleanupPath -and (Test-Path -LiteralPath $cleanupPath)) {
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force
    }
    if ($downloadPath -and (Test-Path -LiteralPath $downloadPath)) {
        Remove-Item -LiteralPath $downloadPath -Force
    }
}
