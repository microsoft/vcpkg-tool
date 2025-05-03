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

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/conflicting-states.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:1: error: 'vcpkg-empty-featureful-port' was already declared as 'pass'
  on expression: vcpkg-empty-featureful-port = cascade
                 ^
$($ciFeatureBaseline):1:1: note: previous declaration was here
  on expression: vcpkg-empty-featureful-port = pass
                 ^
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-combo-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:x64-windows passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-feature-cascade.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a]:x64-windows passed but vcpkg-empty-featureful-port[a] was marked expected to be a cascaded failure
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-feature-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,b]:x64-windows passed but vcpkg-empty-featureful-port[b] was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-port-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,b]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,c]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:x64-windows passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-port-cascade.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core]:x64-windows passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a]:x64-windows passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:x64-windows passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,b]:x64-windows passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,c]:x64-windows passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:x64-windows passed but was marked expected to be a cascaded failure
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-everything.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a]:x64-windows passed but vcpkg-empty-featureful-port[a] was marked expected to be a cascaded failure
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,b]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):3:29: error: vcpkg-empty-featureful-port[core,b]:x64-windows passed but vcpkg-empty-featureful-port[b] was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,c]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):1:29: error: vcpkg-empty-featureful-port[core,c]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a-default-feature,c]:x64-windows passed but was marked expected to fail
$($ciFeatureBaseline):6:29: error: vcpkg-empty-featureful-port[core,a-default-feature,c]:x64-windows passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output
