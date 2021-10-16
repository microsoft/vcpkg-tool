<#
#>
[CmdletBinding(PositionalBinding=$False)]
Param(
    [Parameter(Mandatory=$True)]
    [string]$Commit,

    [Parameter()]
    [string]$GithubRepository = "spdx/license-list-data",

    [Parameter()]
    [string]$LicensesOutFile = "$PSScriptRoot/src/vcpkg/spdx-licenses.inc",

    [Parameter()]
    [string]$ExceptionsOutFile = "$PSScriptRoot/src/vcpkg/spdx-exceptions.inc"
)

function Transform-JsonFile {
    [CmdletBinding()]
    Param(
        [string]$Uri,
        [string]$OutFile,
        [string]$OuterName,
        [string]$Id
    )

    $req = Invoke-WebRequest -Uri $Uri

    if ($req.StatusCode -ne 200)
    {
        Write-Error "Failed to GET $Uri"
        throw
    }

    $json = $req.Content | ConvertFrom-Json -Depth 10
    Write-Verbose "Writing output to $OutFile"

    $fileContent = @(
        "// Data downloaded from $Uri",
        "// Generated by scripts/Generate-SpdxLicenseList.ps1",
        "{")
    $json.$OuterName |
        Sort-Object -Property $Id |
        ForEach-Object {
        $fileContent += "    `"$($_.$Id)`","
    }
    $fileContent += "}"

    $fileContent -join "`n" | Out-File -FilePath $OutFile -Encoding 'utf8'
}

$baseUrl = "https://raw.githubusercontent.com/$GithubRepository/$Commit/json"
Write-Verbose "Getting json files from $baseUrl"

Transform-JsonFile `
    -Uri "$baseUrl/licenses.json" `
    -OutFile $LicensesOutFile `
    -OuterName 'licenses' `
    -Id 'licenseId'

Transform-JsonFile `
    -Uri "$baseUrl/exceptions.json" `
    -OutFile $ExceptionsOutFile `
    -OuterName 'exceptions' `
    -Id 'licenseExceptionId'
