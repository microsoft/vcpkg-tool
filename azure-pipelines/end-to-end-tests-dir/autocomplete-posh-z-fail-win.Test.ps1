
Write-Host "It's spec for Windows."

if ($IsWindows) {
    BeforeAll {
        Import-Module $PSScriptRoot/../../scripts/posh-vcpkg.psd1
    }
    Describe 'Module posh-vcpkg tests' {
        It 'Expect pass' {}
        It 'Expect fail with InvalidResult' {
            Should -Be 'fail'
        }
    }
}
