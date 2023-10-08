# vcpkg_registry_release_process

This document describes the acceptance criteria / process we use when doing a vcpkg registry release.

1. Create a git tag for the release.
1. Submit a full CI rebuild for the tag.
1. Checkout the tag
1. Run vcpkg z-changelog `<SHA OF LAST RELEASE>` > `path/to/results.md`
1. Create a new GitHub release in the registry repo.
1. Run 'auto generate release notes'
1. Change `## New Contributors` to `#### New Contributors`
1. Copy the contents to the end of `path/to/results.md` (the `#### New Contributors` part should line up)
1. Change the link to the full rebuild.
1. Fill out the block about tool release changes.
1. After the full rebuild submission completes, change all the 'Building...'s to actual counts.
1. After a blog post for that release is authored, add a link to the blog post to the release.
