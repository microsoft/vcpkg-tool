# vcpkg_registry_release_process

This document describes the acceptance criteria / process we use when doing a vcpkg registry release.

1. Run vcpkg z-changelog `<SHA OF LAST RELEASE>` > `path/to/results.md`
1. Create a new GitHub release in the registry repo.
1. Run 'auto generate release notes'
1. Change `## New Contributors` to `#### New Contributors`
1. Copy the contents to the end of `path/to/results.md` (the `#### New Contributors` part should line up)
1. Change the link to the most recent CI build, and copy number of successful ports into the table at the top.
1. Fill out the block about tool release changes.
1. Submit a full CI rebuild for the tagged commit.
1. After the full rebuild submission completes, update the link to the one for the exact SHA, the counts, and remove "(tentative)".
1. After a blog post for that release is authored, add a link to the blog post to the release.
