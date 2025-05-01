. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/conflicting-features.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: 'b' was already declared as 'feature-fails'
  on expression: vcpkg-empty-featureful-port[b,c] = cascade
                                             ^
$($ciFeatureBaseline):1:31: note: previous declaration was here
  on expression: vcpkg-empty-featureful-port[a,b] = feature-fails
                                               ^
"@

Throw-IfNonContains -Expected $expected -Actual $output
