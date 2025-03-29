#pragma once

namespace vcpkg
{
    enum class GitRepoLocatorKind
    {
        CurrentDirectory,
        DotGitDir
    };

    enum class GitDiffTreeLineKind
    {
        Added,
        Copied,
        Deleted,
        Modified,
        Renamed,
        TypeChange,
        Unmerged,
        Unknown,
    };

    struct GitRepoLocator;
    struct GitLSTreeEntry;
    struct GitDiffTreeLine;
}
