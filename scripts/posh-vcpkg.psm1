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

    $spaceToComplete = if ($wordToComplete -ne '') { $null }
    elseif ($PSNativeCommandArgumentPassing -in 'Standard', 'Windows') { '' }
    else { '""' }

    [PowerShell]$cmd = [PowerShell]::Create().AddScript{
        Set-Location $args[0]
        & $args[1] autocomplete @($args[2])
    }.AddParameters(($PWD, $commandText, @($textsBeforeCursor + $spaceToComplete)))

    [string[]]$completions = $cmd.Invoke()

    if ($cmd.HadErrors -or $completions.Count -eq 0) {
        return
    }
    else {
        return $completions
    }
}
