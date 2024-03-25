#! /usr/bin/env pwsh

# Start timer
$start_time = Get-Date

# Validate input
if ($args.Count -ne 2) {
    Write-Host "Usage: <script> <directory-to-search> <message-declaration-file>"
    exit 1
}

$SEARCH_DIR = $args[0]
$MESSAGE_FILE = $args[1]
Write-Host "Processing message declarations from $MESSAGE_FILE..."

# Extract message names directly into an array
$declared_messages = @()
Get-Content $MESSAGE_FILE | ForEach-Object {
    if ($_ -match "DECLARE_MESSAGE\(([^,]+),") {
        $messageName = $matches[1].Trim()
        $declared_messages += $messageName
        $messageName
    }
}

# Find all instances of 'msg' prefixed variables in .cpp and .h files and store them in an array
$used_messages = Get-ChildItem -Path $SEARCH_DIR -Include *.cpp, *.h -Recurse |
    Select-String -Pattern 'msg[A-Za-z0-9_]*' -AllMatches |
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

# Report findings
if ($unused_messages.Count -gt 0) {
    Write-Host "Unused messages found."
    foreach ($msg in $unused_messages) {
        Write-Host $msg
    }
}
else {
    Write-Host "All messages are used."
}

Write-Host "Total messages declared: $($declared_messages.Count)"
Write-Host "Total unused messages: $($unused_messages.Count)"

# End timer and calculate elapsed time
$end_time = Get-Date
$elapsed = $end_time - $start_time
Write-Host "Time taken: $($elapsed.TotalSeconds) seconds."

if ($unused_messages.Count -gt 0) {
    Write-Host "Please remove unused messages from $MESSAGE_FILE."
    exit 1
}