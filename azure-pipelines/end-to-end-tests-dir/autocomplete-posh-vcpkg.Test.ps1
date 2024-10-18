BeforeAll {
    Import-Module $PSScriptRoot/../../scripts/posh-vcpkg.psd1
}

Describe 'Module posh-vcpkg tests' {

    BeforeAll {

        function Complete-InputCaret {
            [OutputType([string[]])]
            param (
                [Parameter(Mandatory, Position = 0, ValueFromPipeline)]
                [string]$caretCursorCommand
            )
            $positionMatches = [regex]::Matches($caretCursorCommand, '\^')
            if ($positionMatches.Count -ne 1) {
                throw 'Invalid caret cursor command, please indicate by only one ^ character'
            }
            $command = [string]$caretCursorCommand.Replace('^', '')
            $cursorPosition = [int]$positionMatches[0].Index
            $result = [System.Management.Automation.CommandCompletion]::CompleteInput($command, $cursorPosition, $null)
            # Write-Host ( $result.CompletionMatches | Select-Object -ExpandProperty CompletionText)
            return $result.CompletionMatches | Select-Object -ExpandProperty CompletionText
        }

    }

    Context 'Internal function tests' {

        It 'Complete-InputCaret 1 caret string should success' {
            'aaaa^' | Complete-InputCaret | Should -BeNullOrEmpty
        }

        It 'Complete-InputCaret 0 caret string should throw' {
            { 'aaaa' | Complete-InputCaret } | Should -Throw
        }

        It 'Complete-InputCaret 2 caret string should throw' {
            { 'aaaa^^' | Complete-InputCaret } | Should -Throw
        }

        It 'Complete-InputCaret self should success' {
            'Complete-InputCaret^' | Complete-InputCaret | Should -Contain 'Complete-InputCaret'
        }

    }

    Context 'Complete command name tests' {

        It 'Should complete command list with blank space [vcpkg ^] - <expected>' -ForEach (
            @(
                , 'help'
                , 'install'
                , 'list'
                , 'remove'
                , 'version'
            ) | ForEach-Object { @{Expected = $_ } }
        ) {
            'vcpkg ^' | Complete-InputCaret | Should -Contain $expected
        }

    }



    # Context 'Complete command name tests' {

    #     It -Skip 'Should complete command list with blank space [<caretCmd>]' -TestCases (
    #         @{ caretCmd = 'vcpkg ^'          ; expectedContain = 'version'; comment = 'without word' },
    #         @{ caretCmd = 'vcpkg ver^'       ; expectedContain = 'version'; comment = 'with word' },
    #         @{ caretCmd = 'vcpkg.exe ver^'   ; expectedContain = 'version'; comment = 'with native exe' },
    #         @{ caretCmd = './vcpkg ver^'     ; expectedContain = 'version'; comment = 'with dot slash' },
    #         @{ caretCmd = '.\vcpkg ver^'     ; expectedContain = 'version'; comment = 'with dot backslash' }
    #     ) {
    #         param($caretCmd, $expectedContain, $comment)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    #     It -Skip 'Should complete command word [<expectedContain>] with [<caretCmd>] with <comment>' -TestCases (
    #         @{ caretCmd = 'vcpkg ^'          ; expectedContain = 'version'; comment = 'without word' },
    #         @{ caretCmd = 'vcpkg ver^'       ; expectedContain = 'version'; comment = 'with word' },
    #         @{ caretCmd = 'vcpkg.exe ver^'   ; expectedContain = 'version'; comment = 'with native exe' },
    #         @{ caretCmd = './vcpkg ver^'     ; expectedContain = 'version'; comment = 'with dot slash' },
    #         @{ caretCmd = '.\vcpkg ver^'     ; expectedContain = 'version'; comment = 'with dot backslash' }
    #     ) {
    #         param($caretCmd, $expectedContain, $comment)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    # }

    # Context -Skip 'Complete command spaces tests' {

    #     It 'Should complete command <comment> [<caretCmd>]' -TestCases (
    #         @{ comment = 'spaces without argument'; caretCmd = 'vcpkg     ^'; expectedContain = 'version' },
    #         @{ comment = 'before remaining'; caretCmd = 'vcpkg     ver^'; expectedContain = 'version' },
    #         # @{ comment = 'with trailing spaces'; caretCmd = 'vcpkg ver     ^'; expectedContain = 'version' },
    #         @{ comment = 'with leading spaces'; caretCmd = '     vcpkg ver^'; expectedContain = 'version' }
    #     ) {
    #         param($caretCmd, $expectedContain, $comment)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    #     It 'Should complete command with trailing spaces [vcpkg ver     ^]' -Skip {
    #         'vcpkg ver     ^' | Complete-InputCaret | Should -Contain 'version'
    #     }

    # }

    # Context -Skip 'Complete command quotation tests' {

    #     It "Should complete command with quoted word [vcpkg 'ver'^]" {
    #         "vcpkg 'ver'^" | Complete-InputCaret | Should -Contain 'version'
    #     }

    #     It "Should complete command with quoted space [vcpkg ' '^]" {
    #         "vcpkg 'ver'^" | Complete-InputCaret | Should -Contain 'version'
    #     }

    #     It "Should complete command with quoted word [vcpkg 'version'^]" {
    #         "vcpkg 'ver'^" | Complete-InputCaret | Should -Contain 'version'
    #     }

    # }

    # Context -Skip 'Complete command intermediate tests' {

    #     It 'Should complete command <comment> [<caretCmd>]' -TestCases (
    #         @{ comment = 'end of word'; caretCmd = 'vcpkg version^'; expectedContain = 'version' },
    #         @{ comment = 'middle of word'; caretCmd = 'vcpkg ver^sion'; expectedContain = 'version' },
    #         @{ comment = 'front of word'; caretCmd = 'vcpkg ^version'; expectedContain = 'version' }
    #     ) {
    #         param($caretCmd, $expectedContain, $comment)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    # }

    # Context -Skip 'Complete subcommand tests' {

    #     It 'Should complete subcommand [<expected>] from [<caretCmd>]' -TestCases (
    #         @{ caretCmd = 'vcpkg depend^'; expected = 'depend-info' },
    #         @{ caretCmd = 'vcpkg inst^'; expected = 'install' },
    #         @{ caretCmd = 'vcpkg int^'; expected = 'integrate' },
    #         @{ caretCmd = 'vcpkg rem^'; expected = 'remove' }
    #     ) {
    #         param($caretCmd, $expected)
    #         @($caretCmd | Complete-InputCaret)[0] | Should -BeExactly $expected
    #     }

    #     It 'Should complete subcommand two-level [powershell] from [vcpkg integrate power^]' -Skip {
    #         'vcpkg integrate power^' | Complete-InputCaret | Should -Contain 'powershell'
    #     }

    # }

    # Context -Skip 'Complete subcommand argument and options tests' {

    #     It 'Should complete argument [<expected>] from [<caretCmd>]' -TestCases (
    #         @{ caretCmd = 'vcpkg install vcpkg-cmake^'; expectedContain = 'vcpkg-cmake-get-vars' },
    #         @{ caretCmd = 'vcpkg install vcpkg-cmake --^'; expectedContain = '--dry-run' }
    #     ) {
    #         param($caretCmd, $expected)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    # }

    # Context -Skip 'Complete complex tests' {

    #     It 'Should complete complex line [<expected>] from [<caretCmd>]' -TestCases (
    #         @{ caretCmd = 'echo powershell | % { vcpkg ver^ $_ }; echo $?'; expectedContain = 'version' }
    #     ) {
    #         param($caretCmd, $expected)
    #         $caretCmd | Complete-InputCaret | Should -Contain $expectedContain
    #     }

    # }

}
