Register-ArgumentCompleter -Native -CommandName vcpkg -ScriptBlock {
    param(
        [string]$wordToComplete,
        [System.Management.Automation.Language.CommandAst]$commandAst,
        [int]$cursorPosition
    )

    if ($cursorPosition -lt $commandAst.CommandElements[0].Extent.EndOffset) {
        return
    }

    [string]$commandText = $commandAst.CommandElements[0].Value

    [string[]]$textsBeforeCursor = $commandAst.CommandElements |
        Select-Object -Skip 1 | ForEach-Object {
            if ($_.Extent.EndOffset -le $cursorPosition) {
                $_.Extent.Text
            }
            elseif ($_.Extent.StartOffset -lt $cursorPosition) {
                $_.Extent.Text.Substring(0, $cursorPosition - $_.Extent.StartOffset)
            }
        }

    $spaceToComplete = if ($wordToComplete -eq '') { '' } else { $null }

    if (Get-Command $commandText -ErrorAction SilentlyContinue) {
        $completions = & $commandText autocomplete @textsBeforeCursor $spaceToComplete
        if ($null -eq $completions) {
            return ''
        }
        else {
            return $completions
        }
    }
}
