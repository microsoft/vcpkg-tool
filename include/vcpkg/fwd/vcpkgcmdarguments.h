#pragma once

namespace vcpkg
{
    struct ParsedArguments;
    struct CommandSwitch;
    struct CommandSetting;
    struct CommandMultiSetting;
    struct CommandOptionsStructure;

    enum class AutocompletePriority
    {
        Public,
        Internal,
        Never
    };

    struct CommandMetadata;
    struct HelpTableFormatter;
    struct VcpkgCmdArguments;
    struct FeatureFlagSettings;
    struct PortApplicableSetting;

    enum class CIKind
    {
        None,
        GithubActions,
        GitLabCI,
        AzurePipelines,
        AppVeyor,
        AwsCodeBuild,
        CircleCI,
        HerokuCI,
        JenkinsCI,
        TeamCityCI,
        TravisCI,
        Generic
    };
}
