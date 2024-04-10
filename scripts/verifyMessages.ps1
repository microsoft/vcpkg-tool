#! /usr/bin/env pwsh

# Define paths relative to the script's directory
$SEARCH_DIR = Resolve-Path -Path (Join-Path -Path $PSScriptRoot -ChildPath "..\")
$CPP_MESSAGES = Resolve-Path -Path (Join-Path -Path $PSScriptRoot -ChildPath "..\locales\messages.es.json")
$ARITFACT_MESSAGES = Resolve-Path -Path (Join-Path -Path $PSScriptRoot -ChildPath "..\vcpkg-artifacts\locales\messages.json")

Write-Host "Processing message declarations..."

# Read JSON file into a hashtable to accommodate case-sensitive keys
$jsonContent = Get-Content $CPP_MESSAGES -Raw | ConvertFrom-Json -AsHashTable
$declared_messages = @($jsonContent.Keys) | Where-Object { -not $_.EndsWith('.comment') }

# Read the JSON file with messages to remove into another hashtable
$jsonToRemove = Get-Content $ARITFACT_MESSAGES -Raw | ConvertFrom-Json -AsHashTable
$messages_to_remove = @($jsonToRemove.Keys) | Where-Object { -not $_.EndsWith('.comment') }

# Subtract the artifact messages
$declared_messages = Compare-Object -ReferenceObject $declared_messages -DifferenceObject $messages_to_remove -PassThru

# Find all instances of 'msg' prefixed variables in .cpp and .h files and store them in an array
$used_messages = Get-ChildItem -Path $SEARCH_DIR -Include @('*.cpp', '*.h') -Recurse |
    Select-String -Pattern '\bmsg[A-Za-z0-9_]+\b' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object { $_.Value } |
    Sort-Object -Unique

# Initialize array for unused messages
$unused_messages = @()

# Check each declared message against used messages
foreach ($msg in $declared_messages) {
    $prefixed_msg = "msg$msg"
    if ($used_messages -notcontains $prefixed_msg) {
        $unused_messages += $msg
    }
}

# Remove any empty or whitespace-only entries from unused messages just before reporting
$unused_messages = $unused_messages | Where-Object { ![string]::IsNullOrWhiteSpace($_) }

# Report findings
Write-Host "Total messages declared: $($declared_messages.Count)"
Write-Host "Total unused messages: $($unused_messages.Count)"

if ($unused_messages.Count -gt 0) {
    Write-Host "Please remove the following messages:"
    foreach ($msg in $unused_messages) {
        Write-Host "`n$msg"
    }
    exit 1
}