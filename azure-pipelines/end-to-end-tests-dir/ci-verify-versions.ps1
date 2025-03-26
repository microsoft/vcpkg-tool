. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/ci-verify-versions-registry" "$TestingRoot/ci-verify-versions-registry"
git -C "$TestingRoot/ci-verify-versions-registry" @gitConfigOptions init
git -C "$TestingRoot/ci-verify-versions-registry" @gitConfigOptions add --chmod=+x 'ports/executable-bit/some-script.sh'
git -C "$TestingRoot/ci-verify-versions-registry" @gitConfigOptions add -A
git -C "$TestingRoot/ci-verify-versions-registry" @gitConfigOptions commit -m testing
Move-Item "$TestingRoot/ci-verify-versions-registry/old-port-versions/has-local-edits" "$TestingRoot/ci-verify-versions-registry/ports"

$expected = @"
$TestingRoot/ci-verify-versions-registry/versions/b-/bad-git-tree.json: error: bad-git-tree@1.1 git tree 000000070c5f496fcf1a97cf654d5e81f0d2685a does not match the port directory
$TestingRoot/ci-verify-versions-registry/ports/bad-git-tree: note: the port directory has git tree 6528b2c70c5f496fcf1a97cf654d5e81f0d2685a
$TestingRoot/ci-verify-versions-registry/ports/bad-git-tree/vcpkg.json: note: if bad-git-tree@1.1 is already published, update this file with a new version or port-version, commit it, then add the new version by running:
  vcpkg x-add-version bad-git-tree
  git add versions
  git commit -m `"Update version database`"
note: if bad-git-tree@1.1 is not yet published, overwrite the previous git tree by running:
  vcpkg x-add-version bad-git-tree --overwrite-version
  git add versions
  git commit -m `"Update version database`"
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: bad-git-tree@1.1 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/bad-git-tree/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/bad-history-name: message: bad-history-name@1.1 is correctly in the version database (f34f4ad3dfcc4d46d467d7b6aa04f9732a7951d6)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: bad-history-name@1.1 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/bad-history-name/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-mismatch: message: baseline-version-mismatch@1.1 is correctly in the version database (cf8a1faa9f94f7ceb9513d65093d407e11ac1402)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: error: baseline-version-mismatch is assigned 1.0, but the local port is 1.1
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-mismatch/vcpkg.json: note: baseline-version-mismatch is declared here
note: you can run the following commands to add the current version of baseline-version-mismatch automatically:
  vcpkg x-add-version baseline-version-mismatch
  git add versions
  git commit -m `"Update version database`"
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-mismatch/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-missing: message: baseline-version-missing@1.0 is correctly in the version database (a5c21769008f52ed66afa344f13b786dde4b8d7d)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: error: baseline-version-missing is not assigned a version
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-missing/vcpkg.json: note: baseline-version-missing is declared here
note: you can run the following commands to add the current version of baseline-version-missing automatically:
  vcpkg x-add-version baseline-version-missing
  git add versions
  git commit -m `"Update version database`"
$TestingRoot/ci-verify-versions-registry/ports/baseline-version-missing/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/dependency-not-in-versions-database-feature: message: dependency-not-in-versions-database-feature@1.0 is correctly in the version database (2298ee25ea54ed92595250a2be07d01bdd76f47c)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: dependency-not-in-versions-database-feature@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/dependency-not-in-versions-database-feature/vcpkg.json: error: the dependency no-versions does not exist in the version database; does that port exist?
note: the dependency is in the feature named add-things
$TestingRoot/ci-verify-versions-registry/ports/dependency-not-in-versions-database: message: dependency-not-in-versions-database@1.0 is correctly in the version database (321c8b400526dc412a987285ef469eec6221a4b4)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: dependency-not-in-versions-database@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/dependency-not-in-versions-database/vcpkg.json: error: the dependency no-versions does not exist in the version database; does that port exist?
$TestingRoot/ci-verify-versions-registry/ports/dependency-version-not-in-versions-database-feature: message: dependency-version-not-in-versions-database-feature@1.0 is correctly in the version database (ba3008bb2d42c61f172b7d9592de0212edf20fc6)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: dependency-version-not-in-versions-database-feature@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/dependency-version-not-in-versions-database-feature/vcpkg.json: error: the "version>=" constraint to good names version 0.9 which does not exist in the version database. All versions must exist in the version database to be interpreted by vcpkg.
$TestingRoot/ci-verify-versions-registry/versions/g-/good.json: note: consider removing the version constraint or choosing a value declared here
note: the dependency is in the feature named add-things
$TestingRoot/ci-verify-versions-registry/ports/dependency-version-not-in-versions-database: message: dependency-version-not-in-versions-database@1.0 is correctly in the version database (f0d44555fe7714929e432ab9e12a436e28ffef9e)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: dependency-version-not-in-versions-database@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/dependency-version-not-in-versions-database/vcpkg.json: error: the "version>=" constraint to good names version 0.9 which does not exist in the version database. All versions must exist in the version database to be interpreted by vcpkg.
$TestingRoot/ci-verify-versions-registry/versions/g-/good.json: note: consider removing the version constraint or choosing a value declared here
$TestingRoot/ci-verify-versions-registry/ports/executable-bit: message: executable-bit@1.0 is correctly in the version database (6fb9e388021421a5bf6e2cb1f57c67e9ceb6ee43)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: executable-bit@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/executable-bit/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/good: message: good@1.0 is correctly in the version database (0f3d67db0dbb6aa5499bc09367a606b495e16d35)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: good@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/good/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/has-local-edits: message: has-local-edits@1.0.0 is correctly in the version database (b1d7f6030942b329a200f16c931c01e2ec9e1e79)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: has-local-edits@1.0.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/has-local-edits/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/malformed/vcpkg.json:4:3: error: Unexpected character; expected property name
  on expression:   ~broken
                   ^
$TestingRoot/ci-verify-versions-registry/versions/m-/mismatch-git-tree.json: error: mismatch-git-tree@1.0 git tree 41d20d2a02d75343b0933b624faf9f061b112dad does not match the port directory
$TestingRoot/ci-verify-versions-registry/ports/mismatch-git-tree: note: the port directory has git tree 34b3289caaa7a97950828905d354dc971c3c15a7
$TestingRoot/ci-verify-versions-registry/ports/mismatch-git-tree/vcpkg.json: note: if mismatch-git-tree@1.0 is already published, update this file with a new version or port-version, commit it, then add the new version by running:
  vcpkg x-add-version mismatch-git-tree
  git add versions
  git commit -m `"Update version database`"
note: if mismatch-git-tree@1.0 is not yet published, overwrite the previous git tree by running:
  vcpkg x-add-version mismatch-git-tree --overwrite-version
  git add versions
  git commit -m `"Update version database`"
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: mismatch-git-tree@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/mismatch-git-tree/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/no-versions/vcpkg.json: error: this port is not in the version database
$TestingRoot/ci-verify-versions-registry/versions/n-/no-versions.json: note: the version database file should be here
note: run 'vcpkg x-add-version no-versions' to create the version database file
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: no-versions@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/no-versions/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/override-not-in-versions-database: message: override-not-in-versions-database@1.0 is correctly in the version database (0ff80cd22d5ca881efab3329ce596566a8642bec)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: override-not-in-versions-database@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/override-not-in-versions-database/vcpkg.json: error: the version override no-versions does not exist in the version database; does that port exist?
$TestingRoot/ci-verify-versions-registry/ports/override-version-not-in-versions-database: message: override-version-not-in-versions-database@1.0 is correctly in the version database (49fafaad46408296e50e9d0fd1a3d531bf97d420)
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: override-version-not-in-versions-database@1.0 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/override-version-not-in-versions-database/vcpkg.json: error: the override of good names version 0.9 which does not exist in the version database. Installing this port at the top level will fail as that version will be unresolvable.
$TestingRoot/ci-verify-versions-registry/versions/g-/good.json: note: consider removing the version override or choosing a value declared here
$TestingRoot/ci-verify-versions-registry/ports/version-mismatch/vcpkg.json: error: version-mismatch@1.1 was not found in versions database
$TestingRoot/ci-verify-versions-registry/versions/v-/version-mismatch.json: note: the version should be in this file
note: run 'vcpkg x-add-version version-mismatch' to add the new port version
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: version-mismatch@1.1 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/version-mismatch/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/ports/version-missing/vcpkg.json: error: version-missing@1.1 was not found in versions database
$TestingRoot/ci-verify-versions-registry/versions/v-/version-missing.json: note: the version should be in this file
note: run 'vcpkg x-add-version version-missing' to add the new port version
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: version-missing@1.1 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/version-missing/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/versions/v-/version-scheme-mismatch.json: error: 1.1 is declared version-string, but version-scheme-mismatch is declared with version
$TestingRoot/ci-verify-versions-registry/ports/version-scheme-mismatch/vcpkg.json: note: version-scheme-mismatch is declared here
note: versions must be unique, even if they are declared with different schemes
note: you can overwrite version-scheme-mismatch@1.1 with correct local values by running:
vcpkg x-add-version version-scheme-mismatch --overwrite-version
$TestingRoot/ci-verify-versions-registry/versions/baseline.json: message: version-scheme-mismatch@1.1 matches the current baseline
$TestingRoot/ci-verify-versions-registry/ports/version-scheme-mismatch/vcpkg.json: message: all version constraints are consistent with the version database
$TestingRoot/ci-verify-versions-registry/versions/b-/bad-git-tree.json: error: "C:/Program Files/Git/cmd/git.exe" --git-dir "C:/Dev/work/testing/ci-verify-versions-registry/.git" -c core.autocrlf=false read-tree 000000070c5f496fcf1a97cf654d5e81f0d2685a failed with exit code 128
fatal: failed to unpack tree object 000000070c5f496fcf1a97cf654d5e81f0d2685a
note: while checking out port bad-git-tree with git tree 000000070c5f496fcf1a97cf654d5e81f0d2685a
note: while validating version: 1.1
$TestingRoot/ci-verify-versions-registry/versions/b-/bad-git-tree.json: error: "C:/Program Files/Git/cmd/git.exe" --git-dir "C:/Dev/work/testing/ci-verify-versions-registry/.git" -c core.autocrlf=false read-tree 00000005fb6b76058ce09252f521847363c6b266 failed with exit code 128
fatal: failed to unpack tree object 00000005fb6b76058ce09252f521847363c6b266
note: while checking out port bad-git-tree with git tree 00000005fb6b76058ce09252f521847363c6b266
note: while validating version: 1.0
$TestingRoot/ci-verify-versions-registry/versions/b-/bad-history-name.json: message: bad-history-name@1.1 is correctly in the version database (f34f4ad3dfcc4d46d467d7b6aa04f9732a7951d6)
$TestingRoot/ci-verify-versions-registry/versions/b-/bad-history-name.json: error: db9d98300e7daeb2c0652bae94a0283a1b1a13d1 is declared to contain bad-history-name@1.0, but appears to contain bad-history-name-is-bad@1.0
$TestingRoot/ci-verify-versions-registry/versions/b-/baseline-version-mismatch.json: message: baseline-version-mismatch@1.1 is correctly in the version database (cf8a1faa9f94f7ceb9513d65093d407e11ac1402)
$TestingRoot/ci-verify-versions-registry/versions/b-/baseline-version-mismatch.json: message: baseline-version-mismatch@1.0 is correctly in the version database (a6d7dde2f5a9ea80db16c7f73c43556a7e21e5cf)
$TestingRoot/ci-verify-versions-registry/versions/b-/baseline-version-missing.json: message: baseline-version-missing@1.0 is correctly in the version database (a5c21769008f52ed66afa344f13b786dde4b8d7d)
$TestingRoot/ci-verify-versions-registry/versions/d-/dependency-not-in-versions-database.json: message: dependency-not-in-versions-database@1.0 is correctly in the version database (321c8b400526dc412a987285ef469eec6221a4b4)
$TestingRoot/ci-verify-versions-registry/versions/d-/dependency-not-in-versions-database-feature.json: message: dependency-not-in-versions-database-feature@1.0 is correctly in the version database (2298ee25ea54ed92595250a2be07d01bdd76f47c)
$TestingRoot/ci-verify-versions-registry/versions/d-/dependency-version-not-in-versions-database.json: message: dependency-version-not-in-versions-database@1.0 is correctly in the version database (f0d44555fe7714929e432ab9e12a436e28ffef9e)
$TestingRoot/ci-verify-versions-registry/versions/d-/dependency-version-not-in-versions-database-feature.json: message: dependency-version-not-in-versions-database-feature@1.0 is correctly in the version database (ba3008bb2d42c61f172b7d9592de0212edf20fc6)
$TestingRoot/ci-verify-versions-registry/versions/e-/executable-bit.json: message: executable-bit@1.0 is correctly in the version database (6fb9e388021421a5bf6e2cb1f57c67e9ceb6ee43)
$TestingRoot/ci-verify-versions-registry/versions/g-/good.json: message: good@1.0 is correctly in the version database (0f3d67db0dbb6aa5499bc09367a606b495e16d35)
$TestingRoot/ci-verify-versions-registry/versions/h-/has-local-edits.json: message: has-local-edits@1.0.0 is correctly in the version database (b1d7f6030942b329a200f16c931c01e2ec9e1e79)
$TestingRoot/ci-verify-versions-registry/versions/m-/malformed.json: $buildtreesRoot/versioning_/versions/malformed/a1f22424b0fb1460200c12e1b7933f309f9c8373/vcpkg.json:4:3: error: Unexpected character; expected property name
  on expression:   ~broken
                   ^
note: while validating version: 1.1
$TestingRoot/ci-verify-versions-registry/versions/m-/malformed.json: $buildtreesRoot/versioning_/versions/malformed/72b37802dbdc176ce20b718ce4a332ac38bd0116/vcpkg.json:4:3: error: Unexpected character; expected property name
  on expression:   ~broken
                   ^
note: while validating version: 1.0
$TestingRoot/ci-verify-versions-registry/versions/m-/mismatch-git-tree.json: message: mismatch-git-tree@1.0 is correctly in the version database (41d20d2a02d75343b0933b624faf9f061b112dad)
$TestingRoot/ci-verify-versions-registry/versions/o-/override-not-in-versions-database.json: message: override-not-in-versions-database@1.0 is correctly in the version database (0ff80cd22d5ca881efab3329ce596566a8642bec)
$TestingRoot/ci-verify-versions-registry/versions/o-/override-version-not-in-versions-database.json: message: override-version-not-in-versions-database@1.0 is correctly in the version database (49fafaad46408296e50e9d0fd1a3d531bf97d420)
$TestingRoot/ci-verify-versions-registry/versions/v-/version-mismatch.json: error: 220bdcf2d4836ec9fe867eaff2945b58c08f5618 is declared to contain version-mismatch@1.1-a, but appears to contain version-mismatch@1.1
$TestingRoot/ci-verify-versions-registry/versions/v-/version-mismatch.json: error: 5c1a69be3303fcd085d473d10e311b85202ee93c is declared to contain version-mismatch@1.0-a, but appears to contain version-mismatch@1.0
$TestingRoot/ci-verify-versions-registry/versions/v-/version-missing.json: message: version-missing@1.0 is correctly in the version database (d3b4c8bf4bee7654f63b223a442741bb16f45957)
$TestingRoot/ci-verify-versions-registry/versions/v-/version-scheme-mismatch.json: error: 1.1 is declared version-string, but version-scheme-mismatch@ea2006a1188b81f1f2f6e0aba9bef236d1fb2725 is declared with version
$buildtreesRoot/versioning_/versions/version-scheme-mismatch/ea2006a1188b81f1f2f6e0aba9bef236d1fb2725/vcpkg.json: note: version-scheme-mismatch is declared here
note: versions must be unique, even if they are declared with different schemes
$TestingRoot/ci-verify-versions-registry/versions/v-/version-scheme-mismatch.json: error: 1.0 is declared version-string, but version-scheme-mismatch@89c88798a9fa17ea6753da87887a1fec48c421b0 is declared with version
$buildtreesRoot/versioning_/versions/version-scheme-mismatch/89c88798a9fa17ea6753da87887a1fec48c421b0/vcpkg.json: note: version-scheme-mismatch is declared here
note: versions must be unique, even if they are declared with different schemes
"@

Remove-Problem-Matchers
$actual = Run-VcpkgAndCaptureOutput x-ci-verify-versions @directoryArgs "--x-builtin-ports-root=$TestingRoot/ci-verify-versions-registry/ports" "--x-builtin-registry-versions-dir=$TestingRoot/ci-verify-versions-registry/versions" --verbose --verify-git-trees
Restore-Problem-Matchers
Throw-IfNotFailed

function Sanitize() {
  Param([string]$text)
  $text = $text.Replace('\', '/').Trim()
  $gitCommandRegex = 'error: [^\n]+git[^\n]+failed with exit code 128$' # Git command line has a lot of absolute paths and other machine-dependent state inside
  $text = [System.Text.RegularExpressions.Regex]::Replace($text, $gitCommandRegex, '<<<GIT COMMAND LINE EXITED WITH CODE 128>>>', [System.Text.RegularExpressions.RegexOptions]::Multiline)
  return $text
}

$expected = Sanitize $expected
$actual = Sanitize $actual
Throw-IfNonEqual -Expected $expected -Actual $actual
