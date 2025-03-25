#pragma once

namespace vcpkg
{
    enum class GitRepoLocatorKind
    {
        CurrentDirectory,
        DotGitDir
    };

    struct GitRepoLocator;
    struct GitLSTreeEntry;
}
