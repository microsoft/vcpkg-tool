[CmdletBinding(PositionalBinding=$False)]
Param(
    [Parameter(Mandatory=$True)]
    [String]$DiffFile
)

& git diff ':!vcpkg-artifacts/.npmrc' --output $DiffFile
if (0 -ne (Get-Item -LiteralPath $DiffFile).Length)
{
    Write-Error 'The formatting of the files in the repo were not what we expected, or you forgot to regenerate messages files. Please access the diff from format.diff in the build artifacts, and apply the patch with `git apply`'
    throw
}
