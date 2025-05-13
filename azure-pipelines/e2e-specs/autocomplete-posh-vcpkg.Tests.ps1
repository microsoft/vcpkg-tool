param (
    [Parameter(Mandatory)][string]$poshVcpkgModulePath,
    [Parameter(Mandatory)][System.IO.FileInfo]$vcpkgExe
)

BeforeAll {
    Import-Module $poshVcpkgModulePath

    $env:PATH = $vcpkgExe.Directory.FullName + [System.IO.Path]::PathSeparator + $env:PATH

    function Complete-InputCaret {
        [OutputType([string[]])]
        param (
            [Parameter(Mandatory, Position = 0, ValueFromPipeline)]
            [string]$InputCaretScript
        )
        $positionMatches = [regex]::Matches($InputCaretScript, '\^')
        if ($positionMatches.Count -ne 1) {
            throw 'Invalid caret cursor command, please indicate by only one ^ character'
        }
        $command = [string]$InputCaretScript.Replace('^', '')
        $cursorPosition = [int]$positionMatches[0].Index
        $result = [System.Management.Automation.CommandCompletion]::CompleteInput($command, $cursorPosition, $null)
        return $result.CompletionMatches | Select-Object -ExpandProperty CompletionText
    }

    $VcpkgPredefined = @{
        CommandList         = @(
            'acquire_project', 'acquire', 'activate', 'add', 'create', 'deactivate', 'depend-info', 'edit', 'env'
            'export', 'fetch', 'find', 'format-feature-baseline', 'format-manifest', 'hash', 'help', 'install', 'integrate',
            'license-report', 'list', 'new', 'owns', 'portsdiff', 'remove', 'search', 'update', 'upgrade', 'use', 'version',
            'x-add-version', 'x-check-support', 'x-init-registry', 'x-package-info', 'x-regenerate', 'x-set-installed',
            'x-test-features', 'x-update-baseline', 'x-update-registry', 'x-vsinstances'
        )
        CommonParameterList = @()
        CommandOptionList   = @{
            install = @(
                '--allow-unsupported', '--clean-after-build', '--clean-buildtrees-after-build'
                '--clean-downloads-after-build', '--clean-packages-after-build', '--dry-run', '--editable'
                '--enforce-port-checks', '--head', '--keep-going', '--no-downloads', '--no-print-usage'
                '--only-binarycaching', '--only-downloads', '--recurse', '--x-feature', '--x-no-default-features'
                '--x-prohibit-backcompat-features', '--x-write-nuget-packages-config', '--x-xunit'
            )
            remove  = @(
                '--dry-run', '--outdated', '--purge', '--recurse'
            )
        }
    }
    $VcpkgPredefined | Out-Null
}

Describe 'Prerequisites tests' {
    Context 'Internal function Complete-InputCaret tests' {
        It 'Complete-InputCaret 1 caret string should success' {
            { 'aaaa^' | Complete-InputCaret } | Should -Not -Throw
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
}

Describe 'Complete basic tests' {
    Context 'Complete full command contain tests' {
        It 'Should complete command [vcpkg <_>^] contain [<_>]' -ForEach @(
            'help'
            'install'
            'version'
            'integrate'
        ) {
            'vcpkg {0}^' -f $_ | Complete-InputCaret | Should -Contain $_
        }
    }

    Context 'Complete full command exact match tests' {
        It 'Should exact match command completions [vcpkg <_>^] be [<_>]' -ForEach @(
            'help'
            'install'
            'version'
            'integrate'
        ) {
            'vcpkg {0}^' -f $_ | Complete-InputCaret | Should -Be $_
        }
    }

    Context 'Complete part command contain tests' {
        It 'Should complete command [vcpkg <command>^] contain [<expected>]' -ForEach @(
            @{command = 'he'; expected = 'help' }
            @{command = 'in'; expected = 'install' }
            @{command = 've'; expected = 'version' }
            @{command = 'in'; expected = 'integrate' }
        ) {
            'vcpkg {0}^' -f $command | Complete-InputCaret | Should -Contain $expected
        }
    }

    Context 'Complete space tests' {
        It 'Should complete command for blank space [vcpkg ^] contain [<_>]' -ForEach @(
            'help'
            'install'
            'version'
            'integrate'
        ) {
            'vcpkg ^' | Complete-InputCaret | Should -Contain $_
        }

        It 'Should exact match command completions for blank space [vcpkg ^]' {
            $completions = 'vcpkg ^' | Complete-InputCaret
            $expected = $VcpkgPredefined.CommandList
            Compare-Object $completions $expected | Should -BeNullOrEmpty
        }
    }
}

Describe 'Complete command tests' {
    Context 'Complete common option tests' -Skip {
        It 'Should complete common option for blank space [vcpkg ^] contain [--host-triplet]' {
            'vcpkg ^' | Complete-InputCaret | Should -Contain '--host-triplet'
        }

        It 'Should complete common option for blank space [vcpkg ^] contain [--host-triplet=]' {
            'vcpkg ^' | Complete-InputCaret | Should -Contain '--host-triplet='
        }

        It 'Should complete common option for blank space [vcpkg ^] contain [--vcpkg-root]' {
            'vcpkg ^' | Complete-InputCaret | Should -Contain '--vcpkg-root'
        }

        It 'Should complete common option for blank space [vcpkg ^] contain [--vcpkg-root=]' {
            'vcpkg ^' | Complete-InputCaret | Should -Contain '--vcpkg-root='
        }
    }

    Context 'Complete common option argument tests' -Skip {
        It 'Should complete common option arguments for [vcpkg --triplet^] contain [--triplet=]' {
            'vcpkg --triplet^' -f $argument | Complete-InputCaret | Should -Contain '--triplet=x64-windows'
        }

        It 'Should complete common option arguments for [vcpkg --triplet=^] contain [--triplet=x64-windows]' {
            'vcpkg --triplet=^' -f $argument | Complete-InputCaret | Should -Contain '--triplet=x64-windows'
        }

        It 'Should complete common option arguments for [vcpkg --triplet=x64^] contain [--triplet=x64-windows]' {
            'vcpkg --triplet=x64^' -f $argument | Complete-InputCaret | Should -Contain '--triplet=x64-windows'
        }
    }

    # Skip due to https://github.com/PowerShell/PowerShell/issues/2912
    Context 'Complete command option list tests conditionally - CoreOnly' -Tag CoreOnly {
        It 'Should complete option flags with single minus [vcpkg <command> -^] contain [<expected>]' -ForEach @(
            @{ command = 'install'  ; expected = '--editable' }
            @{ command = 'remove'   ; expected = '--dry-run' }
        ) {
            'vcpkg {0} -^' -f $command | Complete-InputCaret | Should -Contain $expected
        }

        It 'Should complete option flags with double minus [vcpkg <command> --^] contain [<expected>]' -ForEach @(
            @{ command = 'install'  ; expected = '--editable' }
            @{ command = 'remove'   ; expected = '--dry-run' }
        ) {
            'vcpkg {0} --^' -f $command | Complete-InputCaret | Should -Contain $expected
        }

        It 'Should exact match command options for double minus [vcpkg <_> --^]' -ForEach @(
            'install'
            'remove'
        ) {
            $completions = 'vcpkg {0} --^' -f $_ | Complete-InputCaret
            $expected = $VcpkgPredefined.CommandOptionList[$_]
            Compare-Object $completions $expected | Should -BeNullOrEmpty
        }
    }

    Context 'Complete command argument tests conditionally' {
        It 'Should complete install with port name [<caretCmd>] contain [<expected>]' -ForEach @(
            @{ caretCmd = 'vcpkg install vcpkg-^' ; expected = 'vcpkg-cmake' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Contain $expected
        }

        It 'Should complete install port with triplet [<caretCmd>] contain [<expected>]' -ForEach @(
            @{ caretCmd = 'vcpkg install vcpkg-cmake:^' ; expected = 'vcpkg-cmake:x64-windows' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Contain $expected
        }

        It 'Should complete integrate with subcommand [vcpkg integrate inst^] be [install]' {
            'vcpkg integrate inst^' | Complete-InputCaret | Should -Be 'install'
        }

        It 'Should complete integrate with subcommand [vcpkg integrate ^] contain [powershell] - WindowsOnly' -Tag WindowsOnly {
            'vcpkg integrate ^' | Complete-InputCaret | Should -Contain 'powershell'
        }

        It 'Should complete integrate with subcommand [vcpkg integrate ^] contain [bash] - NonWindowsOnly' -Tag NonWindowsOnly {
            'vcpkg integrate ^' | Complete-InputCaret | Should -Contain 'bash'
        }

        It 'Should exact match command subcommands [vcpkg integrate ^] - WindowsOnly' -Tag WindowsOnly {
            $expected = @('install', 'remove', 'powershell', 'project')
            $completions = 'vcpkg integrate ^' | Complete-InputCaret
            Compare-Object $completions $expected | Should -BeNullOrEmpty
        }

        It 'Should exact match command subcommands [vcpkg integrate ^] - NonWindowsOnly' -Tag NonWindowsOnly {
            $expected = @('install', 'remove', 'bash', 'x-fish', 'zsh')
            $completions = 'vcpkg integrate ^' | Complete-InputCaret
            Compare-Object $completions $expected | Should -BeNullOrEmpty
        }
    }
}

Describe 'Complete variants tests' {
    BeforeAll {
        Set-Variable vcpkgWithExt ($vcpkgExe.FullName)
        Set-Variable vcpkgNoExt ([System.IO.Path]::GetFileNameWithoutExtension($vcpkgExe.FullName))
    }

    Context 'Complete basic variant command tests' {
        It 'Should exact match command completions with call operator absolute exe word be [version]' {
            "& $vcpkgWithExt ver^" | Complete-InputCaret | Should -Be 'version'
        }

        It 'Should exact match command completions [<caretCmd>] <comment> be [version]' -ForEach @(
            @{ caretCmd = 'vcpkg ver^'          ; comment = 'with word' }
            @{ caretCmd = '& vcpkg ver^'        ; comment = 'with & word' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Be 'version'
        }

        It 'Should exact match command completions for exe path [<caretCmd>] <comment> be [version]' -ForEach @(
            @{ caretCmd = './vcpkg ver^'        ; comment = 'with dot slash word' }
            @{ caretCmd = '& ./vcpkg ver^'      ; comment = 'with & dot slash word' }
        ) {
            Set-Location $vcpkgExe.Directory
            $caretCmd | Complete-InputCaret | Should -Be 'version'
        }
    }

    Context 'Complete variant command tests conditionally - WindowsOnly' -Tag WindowsOnly {
        It 'Should exact match command completions with call operator no-ext absolute exe word be [version]' {
            "& $vcpkgNoExt ver^" | Complete-InputCaret | Should -Be 'version'
        }

        It 'Should exact match command completions [<caretCmd>] <comment> be [version]' -ForEach @(
            @{ caretCmd = '& vcpkg.exe ver^'    ; comment = 'with & extension word' }
            @{ caretCmd = 'vcpkg.exe ver^'      ; comment = 'with extension word' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Be 'version'
        }

        It 'Should exact match command completions for exe path [<caretCmd>] <comment> be [version]' -ForEach @(
            @{ caretCmd = '.\vcpkg ver^'        ; comment = 'with dot backslash word' }
            @{ caretCmd = '& .\vcpkg ver^'      ; comment = 'with & dot backslash word' }
            @{ caretCmd = './vcpkg.exe ver^'    ; comment = 'with dot slash extension word' }
            @{ caretCmd = '.\vcpkg.exe ver^'    ; comment = 'with dot backslash extension word' }
            @{ caretCmd = '& ./vcpkg.exe ver^'  ; comment = 'with & dot slash extension word' }
            @{ caretCmd = '& .\vcpkg.exe ver^'  ; comment = 'with & dot backslash extension word' }
        ) {
            Set-Location $vcpkgExe.Directory
            $caretCmd | Complete-InputCaret | Should -Be 'version'
        }
    }

    Context 'Complete command with spaces tests' {
        It 'Should complete command [<caretCmd>] <comment> contain [version]' -ForEach @(
            @{ caretCmd = 'vcpkg      ^'        ; comment = 'many spaces' }
            @{ caretCmd = 'vcpkg      ver^'     ; comment = 'middle many spaces' }
            @{ caretCmd = '      vcpkg ver^'    ; comment = 'with leading spaces' }
            @{ caretCmd = '   &   vcpkg ver^'   ; comment = 'with leading spaces and call operator' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Contain 'version'
        }
    }

    Context 'Complete command quotation tests' {
        It "Should fallback to default completion with quoted full word [vcpkg 'install'^]" {
            "vcpkg 'install'^" | Complete-InputCaret | Should -Be $null
        }

        It "Should prevent completion for quoted space [vcpkg ' '^]" {
            "vcpkg ' '^" | Complete-InputCaret | Should -Be $null
        }
    }
}

Describe 'Complete position tests' {
    Context 'Complete command intermediate tests' {
        It 'Should exact match command completions [<caretCmd>] <comment> be [version]' -ForEach @(
            @{ caretCmd = 'vcpkg version^'; comment = 'end of word' }
            @{ caretCmd = 'vcpkg ver^sion'; comment = 'middle of word' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Be 'version'
        }

        It 'Should exact match command completions [<_>]' -ForEach @(
            'vcpkg ^version'
            'vcpkg ^ '
            'vcpkg   ^   '
            '   vcpkg   ^   '
            '   &   vcpkg   ^   '
            'vcpkg   ^   version   '
        ) {
            $completions = $_ | Complete-InputCaret
            $expected = $VcpkgPredefined.CommandList
            Compare-Object $completions $expected | Should -BeNullOrEmpty
        }
    }

    Context 'Complete complex tests' {
        It 'Should complete complex command line [<caretCmd>] be [<expected>]' -ForEach (
            @{ caretCmd = 'echo powershell | % { vcpkg int^egr $_ }; echo $?'; expected = 'integrate' }
        ) {
            $caretCmd | Complete-InputCaret | Should -Be $expected
        }
    }
}

Describe 'Impossible command tests' {
    Context 'Complete non-exist command tests conditionally' {
        It 'Should prevent completion for non-exist command [vcpkg zzzzzzzz^]' {
            'vcpkg zzzzzzzz^' | Complete-InputCaret | Should -Be $null
        }

        It 'Should prevent completion for non-exist command [vcpkg ---^]' {
            'vcpkg ---^' | Complete-InputCaret | Should -Be $null
        }

        It 'Should fallback to default for non-exist command with trailing spaces [vcpkg zzzzzzzz ^] - WindowsOnly' -Tag WindowsOnly {
            'vcpkg zzzzzzzz ^' | Complete-InputCaret | Should -BeLike '.\*'
        }

        It 'Should fallback to default for non-exist command with trailing spaces [vcpkg zzzzzzzz ^] - NonWindowsOnly' -Tag NonWindowsOnly {
            'vcpkg zzzzzzzz ^' | Complete-InputCaret | Should -BeLike './*'
        }
    }

    Context 'Complete error command tests' {
        It 'Should prevent error from error command [vcpkg --triplet^]' {
            { $ErrorActionPreference = 'Stop'; 'vcpkg --triplet^' | Complete-InputCaret } | Should -Not -Throw
        }

        It 'Should prevent completion for error command [vcpkg --triplet^]' {
            'vcpkg --triplet^' | Complete-InputCaret | Should -Be $null
        }
    }
}
