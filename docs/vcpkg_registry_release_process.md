# vcpkg_registry_release_process

This document describes the acceptance criteria / process we use when doing a vcpkg registry release.

1. Make sure the contents of the repo have what you want to release and match a nightly CI run.
1. Create a git tag for the release.
    ```console
    >git fetch origin # Where origin is whichever remote points to microsoft/vcpkg
    >git switch -d <A tested SHA>
    ```
1. Run vcpkg z-changelog `<SHA OF LAST RELEASE>` > `path/to/results.md`
1. Change the link to the full rebuild.
1. Run `Get-BuildSuccessCounts.ps1 -BuildUri https://dev.azure.com/vcpkg/public/_build/results?buildId=<The tested build ID>`
   and update the table in `results.md` with those values.
1. Fill out the block about tool release changes.
1. Fill out the number of ports per platform from the matching rebuild.
1. Mint a git tag for the release. If/when prompted for tag message, paste the current value of
   results.md with the `#`s removed from the first lines.
    ```console
    >git tag 2023.10.19    # Replace this with the date of that SHA
    >git push origin 2023.10.19
    ```
1. Create a new GitHub release in the registry repo on the tag.
1. Run 'auto generate release notes'
1. Change `## New Contributors` to `#### New Contributors`
1. Copy the contents to the end of `path/to/results.md` (the `#### New Contributors` part should line up)
1. Copy `path/to/results.md` into the GitHub release and publish it. (You can delete `path/to/results.md` now :))
1. After a blog post for that release is authored, add a link to the blog post to the release.
