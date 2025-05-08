. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/conflicting-features.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
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
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
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
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:$Triplet passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-feature-cascade.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a]:$Triplet passed but vcpkg-empty-featureful-port[a] was marked expected to be a cascaded failure
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-feature-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,b]:$Triplet passed but vcpkg-empty-featureful-port[b] was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-port-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,b]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,c]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:$Triplet passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-port-cascade.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core]:$Triplet passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a]:$Triplet passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:$Triplet passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,b]:$Triplet passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,c]:$Triplet passed but was marked expected to be a cascaded failure
$($ciFeatureBaseline):2:1: error: vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:$Triplet passed but was marked expected to be a cascaded failure
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-pass-everything.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):2:29: error: vcpkg-empty-featureful-port[core,a]:$Triplet passed but vcpkg-empty-featureful-port[a] was marked expected to be a cascaded failure
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a-default-feature]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,b]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):3:29: error: vcpkg-empty-featureful-port[core,b]:$Triplet passed but vcpkg-empty-featureful-port[b] was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,c]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):1:29: error: vcpkg-empty-featureful-port[core,c]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):5:1: error: vcpkg-empty-featureful-port[core,a-default-feature,c]:$Triplet passed but was marked expected to fail
$($ciFeatureBaseline):6:29: error: vcpkg-empty-featureful-port[core,a-default-feature,c]:$Triplet passed but was marked expected to fail
"@
Throw-IfNonContains -Expected $expected -Actual $output

$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-self-cascade
Throw-IfNotFailed
Throw-IfNonContains -Expected "Feature Test [1/2] vcpkg-self-cascade[core]:$Triplet" -Actual $output
Throw-IfNonContains -Expected "Feature Test [2/2] vcpkg-self-cascade[core,cascade]:$Triplet" -Actual $output
Throw-IfNonContains -Expected @"
Skipping testing of vcpkg-self-cascade[cascade,core,never]:$Triplet@0 because the following dependencies are not supported on $($Triplet):
vcpkg-self-cascade[never]:$Triplet only supports windows & !windows
"@ -Actual $output
Throw-IfNonContains -Expected "error: vcpkg-self-cascade[core,cascade]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-self-cascade[never]:$Triplet only supports windows & !windows" -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-self-cascade.txt"
Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-self-cascade --ci-feature-baseline $ciFeatureBaseline
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-requires-feature
Throw-IfNotFailed
# is-a-default-feature should not be printed here, but we don't currently have a good way to filter out what the
# specific dependency is when extras are brought in indirectly.
# Also, note that there are no 'notes' explaining what might be fixable because there is no feature baseline.
$expected = @"
error: vcpkg-requires-feature[core]:$Triplet build failed but was expected to pass
error: vcpkg-requires-feature[core,a]:$Triplet build failed but was expected to pass
error: vcpkg-requires-feature[core,b]:$Triplet build failed but was expected to pass
error: vcpkg-requires-feature[core,c]:$Triplet build failed but was expected to pass
error: vcpkg-requires-feature[core,fails]:$Triplet build failed but was expected to pass
error: vcpkg-requires-feature[core,cascades]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core]:$Triplet
error: vcpkg-requires-feature[core,a,b,b-required,c,cascades,fails]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core,is-a-default-feature]:$Triplet@0
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-requires-feature-add-required.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-requires-feature --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,fails,b-required]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,cascades,b-required]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core]:$Triplet
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a,b,b-required,c,cascades,fails]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core,is-a-default-feature]:$Triplet@0
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-requires-feature-mark-cascades.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-requires-feature --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-requires-feature[core]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): note: consider adding ``vcpkg-requires-feature=fail``, or ``vcpkg-requires-feature:$Triplet=fail``, or equivalent skips
$($ciFeatureBaseline): note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): note: if this feature succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[a]=combination-fails``
$($ciFeatureBaseline): note: if this feature always fails, consider adding ``vcpkg-requires-feature[a]=feature-fails``, which will mark this test as failing and also remove a from combined feature testing
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,b]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): note: if this feature succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[b]=combination-fails``
$($ciFeatureBaseline): note: if this feature always fails, consider adding ``vcpkg-requires-feature[b]=feature-fails``, which will mark this test as failing and also remove b from combined feature testing
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,c]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): note: if this feature succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[c]=combination-fails``
$($ciFeatureBaseline): note: if this feature always fails, consider adding ``vcpkg-requires-feature[c]=feature-fails``, which will mark this test as failing and also remove c from combined feature testing
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,fails]:$Triplet build failed but was expected to pass
$($ciFeatureBaseline): note: if this feature succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[fails]=combination-fails``
$($ciFeatureBaseline): note: if this feature always fails, consider adding ``vcpkg-requires-feature[fails]=feature-fails``, which will mark this test as failing and also remove fails from combined feature testing
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a,b,b-required,c,fails]:$Triplet build failed but was expected to pass
"@
Throw-IfNonContains -Expected $expected -Actual $output

# This checks for avoiding adding duplicate feature tests when options would turn on the same feature(s) as
# the 'combined' test.

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/bad-options.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfFailed
Throw-IfNonContains -Expected "Feature Test [1/3] vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:$Triplet" -Actual $output
Throw-IfNonContains -Expected "Feature Test [2/3] vcpkg-empty-featureful-port[core,b]:$Triplet" -Actual $output
Throw-IfNonContains -Expected "Feature Test [3/3] vcpkg-empty-featureful-port[core,a,a-default-feature,c]:$Triplet" -Actual $output
