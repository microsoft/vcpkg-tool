# vcpkg_tool_release_process

This document describes the acceptance criteria / process we use when doing a vcpkg-tool update,
such as https://github.com/microsoft/vcpkg/pull/23757

1. Update `$/vcpkg-init/vcpkg-scripts-sha.txt` to the current `master` branch SHA in the registry repo.
2. Verify that all tests etc. are passing in the vcpkg-tool repo's `main` branch, and that the
  contents therein are acceptable for release. (Steps after this will sign code there, so this
  review is responsible gating what has access to code signing.)
3. Check that the changes there are in fact the changes that we want in that release. (Be aware,
  you are responsible for what is about to be signed with a Microsoft code signing certificate by
  proceeding)
4. Submit a new full tree rebuild by microsoft.vcpkg.ci (
  https://dev.azure.com/vcpkg/public/_build?definitionId=29 as of this writing) and queue a new
  build with the vcpkg-tool SHA overridden to the one you wish to use. Example:
  https://dev.azure.com/vcpkg/public/_build/results?buildId=73664&view=results
5. (Probably the next day) Check over the failures and ensure any differences with the most recent
  full rebuild using the previous tool version are understood.
6. Submit a signed build from "vcpkg Signed Binaries (from GitHub)" (
  https://devdiv.visualstudio.com/DevDiv/_build?definitionId=17772&_a=summary as of this writing)
7. The signed build will automatically create a draft GitHub release at
  https://github.com/microsoft/vcpkg-tool/releases . Erase the contents filled in there and press
  the "auto generate release notes" button. Manually remove any entries created by the automated
  localization tools which will start with `* LEGO: Pull request from juno/`.
8. Publish that draft release as "pre-release".
9. Clean up a machine for the following tests:
  * Delete `VCPKG_DOWNLOADS/artifacts` (which forces artifacts to be reacquired)
  * Delete `LOCALAPPDATA/vcpkg` (which forces registries to be reacquired)
10. Smoke test the 'one liner' installer: (Where 2022-06-15 is replaced with the right release name)
    * Powershell:
        `iex (iwr https://github.com/microsoft/vcpkg-tool/releases/download/2022-06-15/vcpkg-init.ps1)`
    * Batch:
        `curl -L -o vcpkg-init.cmd https://github.com/microsoft/vcpkg-tool/releases/download/2022-06-15/vcpkg-init.ps1 && .\vcpkg-init.cmd`
    * Bash:
        `. <(curl https://github.com/microsoft/vcpkg-tool/releases/download/2022-06-15/vcpkg-init -L)`
11. Test that embedded scenarios work for vcpkg-artifacts:
    Ensure that none of the following report errors:
    1. git clone https://github.com/some-example/blink/
    2. cd blink
    3. vcpkg activate
    4. idf.py set-target ESP32
    5. cd build
    6. ninja
12. In the vcpkg repo, draft a PR which updates `bootstrap-vcpkg.sh` and `boostrap-vcpkg.ps1`
  with the new release date, and update SHAs as appropriate in the .sh script. (For example, see
  https://github.com/microsoft/vcpkg/pull/23757)
13. Merge the tool update PR.
14. Change the github release in vcpkg-tool from "prerelease" to "release". (This automatically
  updates the aka.ms links)
