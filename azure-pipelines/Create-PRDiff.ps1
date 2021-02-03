[CmdletBinding(PositionalBinding=$False)]
Param(
    [Parameter(Mandatory=$True)]
    [String]$DiffFile
)

$gitConfigOptions = @(
    '-c', 'user.name=Nobody',
    '-c', 'user.email=nobody@example.com',
    '-c', 'core.autocrlf=false'
)

Start-Process -FilePath 'git' -ArgumentList ($gitConfigOptions + 'diff') `
    -NoNewWindow -Wait `
    -RedirectStandardOutput $DiffFile
if (0 -ne (Get-Item -LiteralPath $DiffFile).Length)
{
    $msg = @(
        'The formatting of the files in the repo were not what we expected.',
        'Please access the diff from format.diff in the build artifacts,'
        'and apply the patch with `git apply`'
    )
    Write-Error ($msg -join "`n")
    throw
}
