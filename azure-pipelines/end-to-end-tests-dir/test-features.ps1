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
note: if vcpkg-requires-feature[fails] succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[core,fails,b-required]:$Triplet=combination-fails``
note: if vcpkg-requires-feature[fails] always fails, consider adding ``vcpkg-requires-feature[fails]:$Triplet=feature-fails``, which will mark this test as failing, and remove vcpkg-requires-feature[fails] from combined feature testing
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,cascades,b-required]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core]:$Triplet
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a,b,b-required,c,cascades,fails]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[core,is-a-default-feature]:$Triplet@0
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-requires-feature-mark-cascades.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-requires-feature --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-requires-feature[core]:$Triplet build failed but was expected to pass
note: consider adding ``vcpkg-requires-feature=fail``, or ``vcpkg-requires-feature:$Triplet=fail``, or equivalent skips
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a]:$Triplet build failed but was expected to pass
note: if vcpkg-requires-feature[a] succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[core,a]:$Triplet=combination-fails``
note: if vcpkg-requires-feature[a] always fails, consider adding ``vcpkg-requires-feature[a]:$Triplet=feature-fails``, which will mark this test as failing, and remove vcpkg-requires-feature[a] from combined feature testing
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,b]:$Triplet build failed but was expected to pass
note: if vcpkg-requires-feature[b] succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[core,b]:$Triplet=combination-fails``
note: if vcpkg-requires-feature[b] always fails, consider adding ``vcpkg-requires-feature[b]:$Triplet=feature-fails``, which will mark this test as failing, and remove vcpkg-requires-feature[b] from combined feature testing
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,c]:$Triplet build failed but was expected to pass
note: if vcpkg-requires-feature[c] succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[core,c]:$Triplet=combination-fails``
note: if vcpkg-requires-feature[c] always fails, consider adding ``vcpkg-requires-feature[c]:$Triplet=feature-fails``, which will mark this test as failing, and remove vcpkg-requires-feature[c] from combined feature testing
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,fails]:$Triplet build failed but was expected to pass
note: if vcpkg-requires-feature[fails] succeeds when built with other features but not alone, consider adding ``vcpkg-requires-feature[core,fails]:$Triplet=combination-fails``
note: if vcpkg-requires-feature[fails] always fails, consider adding ``vcpkg-requires-feature[fails]:$Triplet=feature-fails``, which will mark this test as failing, and remove vcpkg-requires-feature[fails] from combined feature testing
note: if some features are required, consider effectively always enabling those parts in portfile.cmake for vcpkg-requires-feature, or consider adding ``vcpkg-requires-feature[required-feature]=options`` to include 'required-feature' in all tests
$($ciFeatureBaseline): error: vcpkg-requires-feature[core,a,b,b-required,c,fails]:$Triplet build failed but was expected to pass
note: consider adding ``vcpkg-requires-feature=fail``, ``vcpkg-requires-feature:$Triplet=fail``, ``vcpkg-requires-feature[core,a,b,b-required,c,fails]:$Triplet=combination-fails``, or equivalent skips
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-requires-feature-complete.txt"
Run-Vcpkg x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-requires-feature --ci-feature-baseline $ciFeatureBaseline
Throw-IfFailed

# This checks for avoiding adding duplicate feature tests when options would turn on the same feature(s) as
# the 'combined' test.

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/bad-options.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-empty-featureful-port --ci-feature-baseline $ciFeatureBaseline
Throw-IfFailed
Throw-IfNonContains -Expected "Feature Test [1/3] vcpkg-empty-featureful-port[core,a,a-default-feature,b,c]:$Triplet" -Actual $output
Throw-IfNonContains -Expected "Feature Test [2/3] vcpkg-empty-featureful-port[core,b]:$Triplet" -Actual $output
Throw-IfNonContains -Expected "Feature Test [3/3] vcpkg-empty-featureful-port[core,a,a-default-feature,c]:$Triplet" -Actual $output

$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-mutually-incompatible-features
Throw-IfNotFailed
Throw-IfNonContains -Expected "error: vcpkg-mutually-incompatible-features[core,a,b,c,d]:$Triplet build failed but was expected to pass" -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-mutually-incompatible-features-ac-only.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-mutually-incompatible-features --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-mutually-incompatible-features[core,a,b,d]:$Triplet build failed but was expected to pass
note: consider adding ``vcpkg-mutually-incompatible-features=fail``, ``vcpkg-mutually-incompatible-features:$Triplet=fail``, ``vcpkg-mutually-incompatible-features[core,a,b,d]:$Triplet=combination-fails``, or equivalent skips, or by marking mutually exclusive features as options
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-mutually-incompatible-features-bd-only.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-mutually-incompatible-features --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-mutually-incompatible-features[core,a,b,c]:$Triplet build failed but was expected to pass
note: consider adding ``vcpkg-mutually-incompatible-features=fail``, ``vcpkg-mutually-incompatible-features:$Triplet=fail``, ``vcpkg-mutually-incompatible-features[core,a,b,c]:$Triplet=combination-fails``, or equivalent skips, or by marking mutually exclusive features as options
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/vcpkg-mutually-incompatible-features-complete.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-mutually-incompatible-features --ci-feature-baseline $ciFeatureBaseline
Throw-IfFailed
Throw-IfNonContains -Expected "Feature Test [6/6] vcpkg-mutually-incompatible-features[core,a,b]:$Triplet" -Actual $output

$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-depends-on-fail-core
Throw-IfNotFailed
$expected = @"
error: vcpkg-depends-on-fail-core[core]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core]:$Triplet
error: vcpkg-depends-on-fail-core[core,x]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core,is-a-default-feature]:$Triplet@0
"@
Throw-IfNonContains -Expected $expected -Actual $output

$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-depends-on-fail-feature
Throw-IfNotFailed
$expected = @"
error: vcpkg-depends-on-fail-feature[core,x]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[b,core]:$Triplet
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-cascade-combination-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-depends-on-fail-core --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline): error: vcpkg-depends-on-fail-core[core]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core]:$Triplet
$($ciFeatureBaseline):1:28: error: vcpkg-depends-on-fail-core[core,x]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core,is-a-default-feature]:$Triplet@0
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-cascade-port-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-depends-on-fail-core --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):1:1: error: vcpkg-depends-on-fail-core[core]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core]:$Triplet
$($ciFeatureBaseline):1:1: note: consider changing this to =cascade instead
$($ciFeatureBaseline):1:1: error: vcpkg-depends-on-fail-core[core,x]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[a,core,is-a-default-feature]:$Triplet@0
$($ciFeatureBaseline):1:1: note: consider changing this to =cascade instead
"@
Throw-IfNonContains -Expected $expected -Actual $output

$ciFeatureBaseline = "$PSScriptRoot/../e2e-assets/ci-feature-baseline/unexpected-cascade-feature-fail.txt"
$output = Run-VcpkgAndCaptureOutput x-test-features @commonArgs "--x-builtin-ports-root=$PSScriptRoot/../e2e-ports" vcpkg-depends-on-fail-feature --ci-feature-baseline $ciFeatureBaseline
Throw-IfNotFailed
$expected = @"
$($ciFeatureBaseline):1:31: error: vcpkg-depends-on-fail-feature[core,x]:$Triplet was unexpectedly a cascading failure because the following dependencies are unavailable: vcpkg-fail-if-depended-upon[b,core]:$Triplet
$($ciFeatureBaseline):1:31: note: consider changing this to =cascade instead
"@
Throw-IfNonContains -Expected $expected -Actual $output
