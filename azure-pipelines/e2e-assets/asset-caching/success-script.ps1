param (
    [string]$Url,
    [string]$ExpectedSHA512,
    [string]$Destination
)

# Ensure the destination folder exists
$downloadFolder = Split-Path -Path $Destination -Parent
if (-not (Test-Path $downloadFolder)) {
    New-Item -Path $downloadFolder -ItemType Directory | Out-Null
}

# Download the file directly to the specified destination path
Invoke-WebRequest -Uri $Url -OutFile $Destination

# Confirm that the file exists and is accessible
if (-not (Test-Path $Destination)) {
    Throw "Download failed: File not found at $Destination"
    exit 1
}

exit 0