
Write-Host "It's spec for Linux."

if ($IsLinux) {
    BeforeAll {
        Import-Module $PSScriptRoot/posh-vcpkg
    }
    Describe 'Module posh-vcpkg tests' {
        It 'Expect fail with BeforeAll' {}
    }
}
