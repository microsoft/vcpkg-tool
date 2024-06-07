DECLARE_MESSAGE(ABaseline, (), "", "a baseline")
DECLARE_MESSAGE(ABaselineObject, (), "", "a baseline object")
DECLARE_MESSAGE(ADefaultFeature, (), "", "a default feature")
DECLARE_MESSAGE(ABoolean, (), "", "a boolean")
DECLARE_MESSAGE(ABuiltinRegistry, (), "", "a builtin registry")
DECLARE_MESSAGE(AConfigurationObject, (), "", "a configuration object")
DECLARE_MESSAGE(ADependency, (), "", "a dependency")
DECLARE_MESSAGE(ADependencyFeature, (), "", "a feature of a dependency")
DECLARE_MESSAGE(ADemandObject,
                (),
                "'demands' are a concept in the schema of a JSON file the user can edit",
                "a demand object")
DECLARE_MESSAGE(AString, (), "", "a string")
DECLARE_MESSAGE(ADateVersionString, (), "", "a date version string")
DECLARE_MESSAGE(AddArtifactOnlyOne, (msg::command_line), "", "'{command_line}' can only add one artifact at a time.")
DECLARE_MESSAGE(AddCommandFirstArg, (), "", "The first parameter to add must be 'artifact' or 'port'.")
DECLARE_MESSAGE(AddFirstArgument,
                (msg::command_line),
                "",
                "The first argument to '{command_line}' must be 'artifact' or 'port'.")
DECLARE_MESSAGE(AddingCompletionEntry, (msg::path), "", "Adding vcpkg completion entry to {path}.")
DECLARE_MESSAGE(AdditionalPackagesToExport,
                (),
                "",
                "Additional packages (*) need to be exported to complete this operation.")
DECLARE_MESSAGE(AdditionalPackagesToRemove,
                (),
                "",
                "Additional packages (*) need to be removed to complete this operation.")
DECLARE_MESSAGE(APlatformExpression, (), "", "a platform expression")
DECLARE_MESSAGE(AddPortRequiresManifest, (msg::command_line), "", "'{command_line}' requires an active manifest file.")
DECLARE_MESSAGE(AddPortSucceeded, (), "", "Succeeded in adding ports to vcpkg.json file.")
DECLARE_MESSAGE(AddRecurseOption,
                (),
                "",
                "If you are sure you want to remove them, run the command with the --recurse option.")
DECLARE_MESSAGE(AddTripletExpressionNotAllowed,
                (msg::package_name, msg::triplet),
                "",
                "triplet expressions are not allowed here. You may want to change "
                "`{package_name}:{triplet}` to `{package_name}` instead.")
DECLARE_MESSAGE(AddVersionArtifactsOnly,
                (),
                "'--version', and 'vcpkg add port' are command lines that must not be localized",
                "--version is artifacts only and can't be used with vcpkg add port")
DECLARE_MESSAGE(AddVersionAddedVersionToFile, (msg::version, msg::path), "", "added version {version} to {path}")
DECLARE_MESSAGE(AddVersionCommitChangesReminder, (), "", "Did you remember to commit your changes?")
DECLARE_MESSAGE(AddVersionCommitResultReminder, (), "", "Don't forget to commit the result!")
DECLARE_MESSAGE(AddVersionDetectLocalChangesError,
                (),
                "",
                "skipping detection of local changes due to unexpected format in git status output")
DECLARE_MESSAGE(AddVersionFileNotFound, (msg::path), "", "couldn't find required file {path}")
DECLARE_MESSAGE(AddVersionFormatPortSuggestion, (msg::command_line), "", "Run `{command_line}` to format the file")
DECLARE_MESSAGE(AddVersionIgnoringOptionAll,
                (msg::option),
                "The -- before {option} must be preserved as they're part of the help message for the user.",
                "ignoring --{option} since a port name argument was provided")
DECLARE_MESSAGE(AddVersionInstructions,
                (msg::package_name),
                "",
                "you can run the following commands to add the current version of {package_name} automatically:")
DECLARE_MESSAGE(AddVersionNewFile, (), "", "(new file)")
DECLARE_MESSAGE(AddVersionNewShaIs, (msg::commit_sha), "", "new SHA: {commit_sha}")
DECLARE_MESSAGE(AddVersionNoFilesUpdated, (), "", "No files were updated")
DECLARE_MESSAGE(AddVersionNoFilesUpdatedForPort, (msg::package_name), "", "No files were updated for {package_name}")
DECLARE_MESSAGE(AddVersionNoGitSha, (msg::package_name), "", "can't obtain SHA for port {package_name}")
DECLARE_MESSAGE(AddVersionOldShaIs, (msg::commit_sha), "", "old SHA: {commit_sha}")
DECLARE_MESSAGE(AddVersionOverwriteOptionSuggestion,
                (msg::option),
                "The -- before {option} must be preserved as they're a part of the help message for the user.",
                "Use --{option} to bypass this check")
DECLARE_MESSAGE(AddVersionPortFilesShaChanged,
                (msg::package_name),
                "",
                "checked-in files for {package_name} have changed but the version was not updated")
DECLARE_MESSAGE(AddVersionPortFilesShaUnchanged,
                (msg::package_name, msg::version),
                "",
                "checked-in files for {package_name} are unchanged from version {version}")
DECLARE_MESSAGE(AddVersionPortHasImproperFormat, (msg::package_name), "", "{package_name} is not properly formatted")
DECLARE_MESSAGE(AddVersionSuggestVersionDate,
                (msg::package_name),
                "\"version-string\" and \"version-date\" are JSON keys, and --skip-version-format-check is a command "
                "line switch. They should not be translated",
                "The version format of \"{package_name}\" uses \"version-string\", but the format is acceptable as a "
                "\"version-date\". If this format is actually intended to be an ISO 8601 date, change the format to "
                "\"version-date\", and rerun this command. Otherwise, disable this check by rerunning this command and "
                "adding --skip-version-format-check .")
DECLARE_MESSAGE(
    AddVersionSuggestVersionRelaxed,
    (msg::package_name),
    "\"version-string\" and \"version\" are JSON keys, and --skip-version-format-check is a command line switch. They "
    "should not be translated",
    "The version format of \"{package_name}\" uses \"version-string\", but the format is acceptable as a \"version\". "
    "If the versions for this port are orderable using relaxed-version rules, change the format to \"version\", and "
    "rerun this command. Relaxed-version rules order versions by each numeric component. Then, versions with dash "
    "suffixes are sorted lexcographically before. Plus'd build tags are ignored. Examples:\n"
    "1.0 < 1.1-alpha < 1.1-b < 1.1 < 1.1.1 < 1.2+build = 1.2 < 2.0\n"
    "Note in particular that dashed suffixes sort *before*, not after. 1.0-anything < 1.0\n"
    "Note that this sort order is the same as chosen in Semantic Versioning (see https://semver.org), even though the "
    "actually semantic parts do not apply.\n"
    "If versions for this port are not ordered by these rules, disable this check by rerunning this command and adding "
    "--skip-version-format-check .")
DECLARE_MESSAGE(AddVersionUnableToParseVersionsFile, (msg::path), "", "unable to parse versions file {path}")
DECLARE_MESSAGE(AddVersionUncommittedChanges,
                (msg::package_name),
                "",
                "there are uncommitted changes for {package_name}")
DECLARE_MESSAGE(AddVersionUpdateVersionReminder, (), "", "Did you remember to update the version or port version?")
DECLARE_MESSAGE(AddVersionUseOptionAll,
                (msg::command_name, msg::option),
                "The -- before {option} must be preserved as they're part of the help message for the user.",
                "{command_name} with no arguments requires passing --{option} to update all port versions at once")
DECLARE_MESSAGE(AddVersionVersionAlreadyInFile, (msg::version, msg::path), "", "version {version} is already in {path}")
DECLARE_MESSAGE(AddVersionVersionIs, (msg::version), "", "version: {version}")
DECLARE_MESSAGE(ADictionaryOfContacts, (), "", "a dictionary of contacts")
DECLARE_MESSAGE(AFeature, (), "", "a feature")
DECLARE_MESSAGE(AFeatureName, (), "", "a feature name")
DECLARE_MESSAGE(AFilesystemRegistry, (), "", "a filesystem registry")
DECLARE_MESSAGE(AGitObjectSha, (), "", "a git object SHA")
DECLARE_MESSAGE(AGitReference, (), "", "a git reference (for example, a branch)")
DECLARE_MESSAGE(AGitRegistry, (), "", "a git registry")
DECLARE_MESSAGE(AGitRepositoryUrl, (), "", "a git repository URL")
DECLARE_MESSAGE(AllFormatArgsRawArgument,
                (msg::value),
                "example of {value} is 'foo {} bar'",
                "format string \"{value}\" contains a raw format argument")
DECLARE_MESSAGE(AllFormatArgsUnbalancedBraces,
                (msg::value),
                "example of {value} is 'foo bar {'",
                "unbalanced brace in format string \"{value}\"")
DECLARE_MESSAGE(AllPackagesAreUpdated, (), "", "All installed packages are up-to-date with the local portfile.")
DECLARE_MESSAGE(AlreadyInstalled, (msg::spec), "", "{spec} is already installed")
DECLARE_MESSAGE(AlreadyInstalledNotHead,
                (msg::spec),
                "'HEAD' means the most recent version of source code",
                "{spec} is already installed -- not building from HEAD")
DECLARE_MESSAGE(AManifest, (), "", "a manifest")
DECLARE_MESSAGE(AMaximumOfOneAssetReadUrlCanBeSpecified, (), "", "a maximum of one asset read url can be specified.")
DECLARE_MESSAGE(AMaximumOfOneAssetWriteUrlCanBeSpecified, (), "", "a maximum of one asset write url can be specified.")
DECLARE_MESSAGE(AmbiguousConfigDeleteConfigFile,
                (msg::path),
                "",
                "Ambiguous vcpkg configuration provided by both manifest and configuration file.\n-- Delete "
                "configuration file {path}")
DECLARE_MESSAGE(AnArtifactsGitRegistryUrl, (), "", "an artifacts git registry URL")
DECLARE_MESSAGE(AnArtifactsRegistry, (), "", "an artifacts registry")
DECLARE_MESSAGE(AnArrayOfDefaultFeatures, (), "", "an array of default features")
DECLARE_MESSAGE(AnArrayOfDependencies, (), "", "an array of dependencies")
DECLARE_MESSAGE(AnArrayOfDependencyOverrides, (), "", "an array of dependency overrides")
DECLARE_MESSAGE(AnArrayOfFeatures, (), "", "an array of features")
DECLARE_MESSAGE(AnArrayOfIdentifers, (), "", "an array of identifiers")
DECLARE_MESSAGE(AnArrayOfOverlayPaths, (), "", "an array of overlay paths")
DECLARE_MESSAGE(AnArrayOfOverlayTripletsPaths, (), "", "an array of overlay triplets paths")
DECLARE_MESSAGE(AnArrayOfRegistries, (), "", "an array of registries")
DECLARE_MESSAGE(AnArrayOfVersions, (), "", "an array of versions")
DECLARE_MESSAGE(AndroidHomeDirMissingProps,
                (msg::env_var, msg::path),
                "Note: 'source.properties' is code and should not be translated.",
                "source.properties missing in {env_var} directory: {path}")
DECLARE_MESSAGE(AnExactVersionString, (), "", "an exact version string")
DECLARE_MESSAGE(AnIdentifer, (), "", "an identifier")
DECLARE_MESSAGE(AnObjectContainingVcpkgArtifactsMetadata,
                (),
                "'vcpkg-artifacts' is the name of the product feature and should not be localized",
                "an object containing vcpkg-artifacts metadata")
DECLARE_MESSAGE(AnOverlayPath, (), "", "an overlay path")
DECLARE_MESSAGE(AnOverlayTripletsPath, (), "", "a triplet path")
DECLARE_MESSAGE(AnOverride, (), "", "an override")
DECLARE_MESSAGE(ANonNegativeInteger, (), "", "a nonnegative integer")
DECLARE_MESSAGE(AnotherInstallationInProgress,
                (),
                "",
                "Another installation is in progress on the machine, sleeping 6s before retrying.")
DECLARE_MESSAGE(AnSpdxLicenseExpression, (), "", "an SPDX license expression")
DECLARE_MESSAGE(APackageName, (), "", "a package name")
DECLARE_MESSAGE(APackagePattern, (), "", "a package pattern")
DECLARE_MESSAGE(APackagePatternArray, (), "", "a package pattern array")
DECLARE_MESSAGE(APath, (), "", "a path")
DECLARE_MESSAGE(AppliedUserIntegration, (), "", "Applied user-wide integration for this vcpkg root.")
DECLARE_MESSAGE(ApplocalProcessing, (), "", "deploying dependencies")
DECLARE_MESSAGE(ARegistry, (), "", "a registry")
DECLARE_MESSAGE(ARegistryImplementationKind, (), "", "a registry implementation kind")
DECLARE_MESSAGE(ARegistryPath, (), "", "a registry path")
DECLARE_MESSAGE(ARegistryPathMustBeDelimitedWithForwardSlashes,
                (),
                "",
                "A registry path must use single forward slashes as path separators.")
DECLARE_MESSAGE(ARegistryPathMustNotHaveDots, (), "", "A registry path must not have 'dot' or 'dot dot' path elements.")
DECLARE_MESSAGE(ARegistryPathMustStartWithDollar,
                (),
                "",
                "A registry path must start with `$` to mean the registry root; for example, `$/foo/bar`.")
DECLARE_MESSAGE(ARelaxedVersionString, (), "", "a relaxed version string")
DECLARE_MESSAGE(ArtifactsBootstrapFailed, (), "", "vcpkg-artifacts is not installed and could not be bootstrapped.")
DECLARE_MESSAGE(ArtifactsNotInstalledReadonlyRoot,
                (),
                "",
                "vcpkg-artifacts is not installed, and it can't be installed because VCPKG_ROOT is assumed to be "
                "readonly. Reinstalling vcpkg using the 'one liner' may fix this problem.")
DECLARE_MESSAGE(ArtifactsOptionIncompatibility, (msg::option), "", "--{option} has no effect on find artifact.")
DECLARE_MESSAGE(ArtifactsOptionJson,
                (),
                "",
                "Full path to JSON file where environment variables and other properties are recorded")
DECLARE_MESSAGE(ArtifactsOptionMSBuildProps,
                (),
                "",
                "Full path to the file in which MSBuild properties will be written")
DECLARE_MESSAGE(ArtifactsOptionVersion, (), "", "A version or version range to match; only valid for artifacts")
DECLARE_MESSAGE(ArtifactsOptionVersionMismatch,
                (),
                "--version is a command line switch and must not be localized",
                "The number of --version switches must match the number of named artifacts")
DECLARE_MESSAGE(ArtifactsSwitchAllLanguages, (), "", "Acquires all language files when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchARM, (), "", "Forces host detection to ARM when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchARM64, (), "", "Forces host detection to ARM64 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchForce, (), "", "Forces reacquire if an artifact is already acquired")
DECLARE_MESSAGE(ArtifactsSwitchFreebsd, (), "", "Forces host detection to FreeBSD when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchLinux, (), "", "Forces host detection to Linux when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchTargetARM, (), "", "Sets target detection to ARM when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchTargetARM64, (), "", "Sets target detection to ARM64 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchTargetX64, (), "", "Sets target detection to x64 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchTargetX86, (), "", "Sets target to x86 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchOnlyOneOperatingSystem,
                (),
                "The words after -- are command line switches and must not be localized.",
                "Only one operating system (--windows, --osx, --linux, --freebsd) may be set.")
DECLARE_MESSAGE(ArtifactsSwitchOnlyOneHostPlatform,
                (),
                "The words after -- are command line switches and must not be localized.",
                "Only one host platform (--x64, --x86, --arm, --arm64) may be set.")
DECLARE_MESSAGE(ArtifactsSwitchOnlyOneTargetPlatform,
                (),
                "The words after -- are command line switches and must not be localized.",
                "Only one target platform (--target:x64, --target:x86, --target:arm, --target:arm64) may be set.")
DECLARE_MESSAGE(ArtifactsSwitchOsx, (), "", "Forces host detection to MacOS when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchX64, (), "", "Forces host detection to x64 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchX86, (), "", "Forces host detection to x86 when acquiring artifacts")
DECLARE_MESSAGE(ArtifactsSwitchWindows, (), "", "Forces host detection to Windows when acquiring artifacts")
DECLARE_MESSAGE(AssetCacheProviderAcceptsNoArguments,
                (msg::value),
                "{value} is a asset caching provider name such as azurl, clear, or x-block-origin",
                "unexpected arguments: '{value}' does not accept arguments")
DECLARE_MESSAGE(AssetSourcesArg, (), "", "Asset caching sources. See 'vcpkg help assetcaching'")
DECLARE_MESSAGE(ASemanticVersionString, (), "", "a semantic version string")
DECLARE_MESSAGE(ASetOfFeatures, (), "", "a set of features")
DECLARE_MESSAGE(AStringOrArrayOfStrings, (), "", "a string or array of strings")
DECLARE_MESSAGE(AStringStringDictionary, (), "", "a \"string\": \"string\" dictionary")
DECLARE_MESSAGE(AttemptingToSetBuiltInBaseline,
                (),
                "",
                "attempting to set builtin-baseline in vcpkg.json while overriding the default-registry in "
                "vcpkg-configuration.json.\nthe default-registry from vcpkg-configuration.json will be used.")
DECLARE_MESSAGE(AuthenticationMayRequireManualAction,
                (msg::vendor),
                "",
                "One or more {vendor} credential providers requested manual action. Add the binary source "
                "'interactive' to allow interactivity.")
DECLARE_MESSAGE(AutomaticLinkingForMSBuildProjects,
                (),
                "",
                "All MSBuild C++ projects can now #include any installed libraries. Linking will be handled "
                "automatically. Installing new libraries will make them instantly available.")
DECLARE_MESSAGE(AutomaticLinkingForVS2017AndLater,
                (),
                "",
                "Visual Studio 2017 and later can now #include any installed libraries. Linking will be handled "
                "automatically. Installing new libraries will make them instantly available.")
DECLARE_MESSAGE(AutoSettingEnvVar,
                (msg::env_var, msg::url),
                "An example of env_var is \"HTTP(S)_PROXY\""
                "'--' at the beginning must be preserved",
                "-- Automatically setting {env_var} environment variables to \"{url}\".")
DECLARE_MESSAGE(AUrl, (), "", "a url")
DECLARE_MESSAGE(AvailableHelpTopics, (), "", "Available help topics:")
DECLARE_MESSAGE(AVcpkgRepositoryCommit, (), "", "a vcpkg repository commit")
DECLARE_MESSAGE(AVersionDatabaseEntry, (), "", "a version database entry")
DECLARE_MESSAGE(AVersionObject, (), "", "a version object")
DECLARE_MESSAGE(AVersionOfAnyType, (), "", "a version of any type")
DECLARE_MESSAGE(AVersionConstraint, (), "", "a version constraint")
DECLARE_MESSAGE(AzUrlAssetCacheRequiresBaseUrl,
                (),
                "",
                "unexpected arguments: asset config 'azurl' requires a base url")
DECLARE_MESSAGE(AzUrlAssetCacheRequiresLessThanFour,
                (),
                "",
                "unexpected arguments: asset config 'azurl' requires fewer than 4 arguments")
DECLARE_MESSAGE(BaselineConflict,
                (),
                "",
                "Specifying vcpkg-configuration.default-registry in a manifest file conflicts with built-in "
                "baseline.\nPlease remove one of these conflicting settings.")
DECLARE_MESSAGE(BaselineGitShowFailed,
                (msg::commit_sha),
                "",
                "while checking out baseline from commit '{commit_sha}', failed to `git show` "
                "versions/baseline.json. This may be fixed by fetching commits with `git fetch`.")
DECLARE_MESSAGE(BaselineMissing, (msg::package_name), "", "{package_name} is not assigned a version")
DECLARE_MESSAGE(BinarySourcesArg,
                (),
                "'vcpkg help binarycaching' is a command line and should not be localized",
                "Binary caching sources. See 'vcpkg help binarycaching'")
DECLARE_MESSAGE(BinaryWithInvalidArchitecture,
                (msg::path, msg::expected, msg::actual),
                "{expected} and {actual} are architectures",
                "{path}\n Expected: {expected}, but was {actual}")
DECLARE_MESSAGE(BuildAlreadyInstalled,
                (msg::spec),
                "",
                "{spec} is already installed; please remove {spec} before attempting to build it.")
DECLARE_MESSAGE(BuildDependenciesMissing,
                (),
                "",
                "The build command requires all dependencies to be already installed.\nThe following "
                "dependencies are missing:")
DECLARE_MESSAGE(BuildingFromHead,
                (msg::spec),
                "'HEAD' means the most recent version of source code",
                "Building {spec} from HEAD...")
DECLARE_MESSAGE(BuildingPackage, (msg::spec), "", "Building {spec}...")
DECLARE_MESSAGE(BuildingPackageFailed,
                (msg::spec, msg::build_result),
                "",
                "building {spec} failed with: {build_result}")
DECLARE_MESSAGE(BuildingPackageFailedDueToMissingDeps,
                (),
                "Printed after BuildingPackageFailed, and followed by a list of dependencies that were missing.",
                "due to the following missing dependencies:")
DECLARE_MESSAGE(BuildResultBuildFailed,
                (),
                "Printed after the name of an installed entity to indicate that it failed to build.",
                "BUILD_FAILED")
DECLARE_MESSAGE(
    BuildResultCacheMissing,
    (),
    "Printed after the name of an installed entity to indicate that it was not present in the binary cache when "
    "the user has requested that things may only be installed from the cache rather than built.",
    "CACHE_MISSING")
DECLARE_MESSAGE(BuildResultCascadeDueToMissingDependencies,
                (),
                "Printed after the name of an installed entity to indicate that it could not attempt "
                "to be installed because one of its transitive dependencies failed to install.",
                "CASCADED_DUE_TO_MISSING_DEPENDENCIES")
DECLARE_MESSAGE(BuildResultDownloaded,
                (),
                "Printed after the name of an installed entity to indicate that it was successfully "
                "downloaded but no build or install was requested.",
                "DOWNLOADED")
DECLARE_MESSAGE(BuildResultExcluded,
                (),
                "Printed after the name of an installed entity to indicate that the user explicitly "
                "requested it not be installed.",
                "EXCLUDED")
DECLARE_MESSAGE(
    BuildResultFileConflicts,
    (),
    "Printed after the name of an installed entity to indicate that it conflicts with something already installed",
    "FILE_CONFLICTS")
DECLARE_MESSAGE(BuildResultPostBuildChecksFailed,
                (),
                "Printed after the name of an installed entity to indicate that it built "
                "successfully, but that it failed post build checks.",
                "POST_BUILD_CHECKS_FAILED")
DECLARE_MESSAGE(BuildResultRemoved,
                (),
                "Printed after the name of an uninstalled entity to indicate that it was successfully uninstalled.",
                "REMOVED")
DECLARE_MESSAGE(
    BuildResultSucceeded,
    (),
    "Printed after the name of an installed entity to indicate that it was built and installed successfully.",
    "SUCCEEDED")
DECLARE_MESSAGE(BuildResultSummaryHeader,
                (msg::triplet),
                "Displayed before a list of a summary installation results.",
                "SUMMARY FOR {triplet}")
DECLARE_MESSAGE(BuildResultSummaryLine,
                (msg::build_result, msg::count),
                "Displayed to show a count of results of a build_result in a summary.",
                "{build_result}: {count}")
DECLARE_MESSAGE(BuildTreesRootDir, (), "", "Buildtrees directory (experimental)")
DECLARE_MESSAGE(BuildTroubleshootingMessage1,
                (),
                "First part of build troubleshooting message, printed before the URI to look for existing bugs.",
                "Please ensure you're using the latest port files with `git pull` and `vcpkg "
                "update`.\nThen check for known issues at:")
DECLARE_MESSAGE(BuildTroubleshootingMessage2,
                (),
                "Second part of build troubleshooting message, printed after the URI to look for "
                "existing bugs but before the URI to file one.",
                "You can submit a new issue at:")
DECLARE_MESSAGE(BuildTroubleshootingMessageGH,
                (),
                "Another part of build troubleshooting message, printed after the URI. An alternative version to "
                "create an issue in some cases.",
                "You can also submit an issue by running (GitHub CLI must be installed):")
DECLARE_MESSAGE(
    BuildTroubleshootingMessage3,
    (msg::package_name),
    "Third part of build troubleshooting message, printed after the URI to file a bug but "
    "before version information about vcpkg itself.",
    "Include '[{package_name}] Build error' in your bug report title, the following version information in your "
    "bug description, and attach any relevant failure logs from above.")
DECLARE_MESSAGE(BuiltInTriplets, (), "", "Built-in Triplets:")
DECLARE_MESSAGE(BuiltWithIncorrectArchitecture, (), "", "The following files were built for an incorrect architecture:")
DECLARE_MESSAGE(ChecksFailedCheck, (), "", "vcpkg has crashed; no additional details are available.")
DECLARE_MESSAGE(ChecksUnreachableCode, (), "", "unreachable code was reached")
DECLARE_MESSAGE(ChecksUpdateVcpkg, (), "", "updating vcpkg by rerunning bootstrap-vcpkg may resolve this failure.")
DECLARE_MESSAGE(CiBaselineAllowUnexpectedPassingRequiresBaseline,
                (),
                "",
                "--allow-unexpected-passing can only be used if a baseline is provided via --ci-baseline.")
DECLARE_MESSAGE(CiBaselineDisallowedCascade,
                (msg::spec, msg::path),
                "",
                "REGRESSION: {spec} cascaded, but it is required to pass. ({path}).")
DECLARE_MESSAGE(CiBaselineIndependentRegression,
                (msg::spec, msg::build_result),
                "",
                "REGRESSION: Independent {spec} failed with {build_result}.")
DECLARE_MESSAGE(CiBaselineRegression,
                (msg::spec, msg::build_result, msg::path),
                "",
                "REGRESSION: {spec} failed with {build_result}. If expected, add {spec}=fail to {path}.")
DECLARE_MESSAGE(CiBaselineRegressionHeader,
                (),
                "Printed before a series of CiBaselineRegression and/or CiBaselineUnexpectedPass messages.",
                "REGRESSIONS:")
DECLARE_MESSAGE(CiBaselineUnexpectedFail,
                (msg::spec, msg::triplet),
                "",
                "REGRESSION: {spec} is marked as fail but not supported for {triplet}.")
DECLARE_MESSAGE(CiBaselineUnexpectedFailCascade,
                (msg::spec, msg::triplet),
                "",
                "REGRESSION: {spec} is marked as fail but one dependency is not supported for {triplet}.")
DECLARE_MESSAGE(CiBaselineUnexpectedPass,
                (msg::spec, msg::path),
                "",
                "PASSING, REMOVE FROM FAIL LIST: {spec} ({path}).")
DECLARE_MESSAGE(CISettingsExclude, (), "", "Comma-separated list of ports to skip")
DECLARE_MESSAGE(CISettingsOptCIBase,
                (),
                "",
                "Path to the ci.baseline.txt file. Used to skip ports and detect regressions.")
DECLARE_MESSAGE(CISettingsOptExclude, (), "", "Comma separated list of ports to skip")
DECLARE_MESSAGE(CISettingsOptFailureLogs, (), "", "Directory to which failure logs will be copied")
DECLARE_MESSAGE(CISettingsOptHostExclude, (), "", "Comma separated list of ports to skip for the host triplet")
DECLARE_MESSAGE(CISettingsOptOutputHashes, (), "", "File to output all determined package hashes")
DECLARE_MESSAGE(CISettingsOptParentHashes,
                (),
                "",
                "File to read package hashes for a parent CI state, to reduce the set of changed packages")
DECLARE_MESSAGE(CISettingsOptXUnit, (), "", "File to output results in XUnit format")
DECLARE_MESSAGE(CISettingsVerifyGitTree,
                (),
                "",
                "Verifies that each git tree object matches its declared version (this is very slow)")
DECLARE_MESSAGE(CISettingsVerifyVersion, (), "", "Prints result for each port rather than only just errors")
DECLARE_MESSAGE(CISkipInstallation, (), "", "The following packages are already installed and won't be built again:")
DECLARE_MESSAGE(CISwitchOptAllowUnexpectedPassing, (), "", "Suppresses 'Passing, remove from fail list' results")
DECLARE_MESSAGE(CISwitchOptDryRun, (), "", "Prints out plan without execution")
DECLARE_MESSAGE(CISwitchOptRandomize, (), "", "Randomizes the install order")
DECLARE_MESSAGE(CISwitchOptSkipFailures,
                (),
                "=fail is an on-disk format and should not be localized",
                "Skips ports marked `=fail` in ci.baseline.txt")
DECLARE_MESSAGE(CISwitchOptXUnitAll, (), "", "Reports unchanged ports in the XUnit output")
DECLARE_MESSAGE(ClearingContents, (msg::path), "", "Clearing contents of {path}")
DECLARE_MESSAGE(CMakePkgConfigTargetsUsage, (msg::package_name), "", "{package_name} provides pkg-config modules:")
DECLARE_MESSAGE(CmakeTargetsExcluded, (msg::count), "", "{count} additional targets are not displayed.")
DECLARE_MESSAGE(CMakeTargetsUsage,
                (msg::package_name),
                "'targets' are a CMake and Makefile concept",
                "{package_name} provides CMake targets:")
DECLARE_MESSAGE(
    CMakeTargetsUsageHeuristicMessage,
    (),
    "Displayed after CMakeTargetsUsage; the # must be kept at the beginning so that the message remains a comment.",
    "# this is heuristically generated, and may not be correct")
DECLARE_MESSAGE(CMakeToolChainFile, (msg::path), "", "CMake projects should use: \"-DCMAKE_TOOLCHAIN_FILE={path}\"")
DECLARE_MESSAGE(CMakeUsingExportedLibs,
                (msg::value),
                "{value} is a CMake command line switch of the form -DFOO=BAR",
                "To use exported libraries in CMake projects, add {value} to your CMake command line.")
DECLARE_MESSAGE(CmdAcquireExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg acquire <artifact>")
DECLARE_MESSAGE(CmdAcquireProjectSynopsis, (), "", "Acquires all artifacts referenced by a manifest")
DECLARE_MESSAGE(CmdAcquireSynopsis, (), "", "Acquires the named artifact")
DECLARE_MESSAGE(CmdActivateSynopsis, (), "", "Activates artifacts from a manifest")
DECLARE_MESSAGE(CmdAddExample1, (), "", "vcpkg add port <port name>")
DECLARE_MESSAGE(CmdAddExample2, (), "", "vcpkg add artifact <artifact name>")
DECLARE_MESSAGE(CmdAddSynopsis, (), "", "Adds dependency to manifest")
DECLARE_MESSAGE(CmdAddVersionSynopsis, (), "", "Adds a version to the version database")
DECLARE_MESSAGE(CmdAddVersionExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg x-add-version <port name>")
DECLARE_MESSAGE(CmdAddVersionOptAll, (), "", "Processes versions for all ports")
DECLARE_MESSAGE(CmdAddVersionOptOverwriteVersion, (), "", "Overwrites git-tree of an existing version")
DECLARE_MESSAGE(CmdAddVersionOptSkipFormatChk, (), "", "Skips the formatting check of vcpkg.json files")
DECLARE_MESSAGE(CmdAddVersionOptSkipVersionFormatChk, (), "", "Skips the version format check")
DECLARE_MESSAGE(CmdAddVersionOptVerbose, (), "", "Prints success messages rather than only errors")
DECLARE_MESSAGE(CmdBootstrapStandaloneSynopsis, (), "", "Bootstraps a vcpkg root from only a vcpkg binary")
DECLARE_MESSAGE(CmdBuildExternalExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg build-external <port name> <source path>")
DECLARE_MESSAGE(CmdBuildExternalExample2,
                (),
                "This is a command line, only the path part should be changed to a path conveying the same idea",
                "vcpkg build-external zlib2 C:\\path\\to\\dir\\with\\vcpkg.json")
DECLARE_MESSAGE(CmdBuildExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg build <port spec>")
DECLARE_MESSAGE(CmdBuildExternalSynopsis, (), "", "Builds port from a path")
DECLARE_MESSAGE(CmdBuildSynopsis, (), "", "Builds a port")
DECLARE_MESSAGE(CmdCiCleanSynopsis,
                (),
                "CI is continuous integration (building everything together)",
                "Clears all files to prepare for a CI run")
DECLARE_MESSAGE(CmdCiSynopsis,
                (),
                "CI is continuous integration (building everything together)",
                "Tries building all ports for CI testing")
DECLARE_MESSAGE(CmdCiVerifyVersionsSynopsis, (), "", "Checks integrity of the version database")
DECLARE_MESSAGE(CmdCheckSupportExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg x-check-support <port name>")
DECLARE_MESSAGE(CmdCheckSupportSynopsis, (), "", "Tests whether a port is supported without building it")
DECLARE_MESSAGE(CmdCreateExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg create <port name> <uri>")
DECLARE_MESSAGE(CmdCreateExample2,
                (),
                "This is a command line, 'my-fancy-port' and 'sources.zip' should probably be localized",
                "vcpkg create my-fancy-port https://example.com/sources.zip")
DECLARE_MESSAGE(CmdCreateExample3,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg create <port name> <uri> <downloaded filename>")
DECLARE_MESSAGE(CmdDeactivateSynopsis, (), "", "Removes all artifact activations from the current shell")
DECLARE_MESSAGE(CmdDependInfoExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg depend-info <port name>")
DECLARE_MESSAGE(CmdDependInfoFormatConflict,
                (),
                "",
                "Conflicting formats specified. Only one of --format, --dgml, or --dot are accepted.")
DECLARE_MESSAGE(CmdDependInfoFormatHelp,
                (),
                "The alternatives in ``s must not be localized.",
                "Chooses output format, one of `list`, `tree`, `mermaid`, `dot`, or `dgml`")
DECLARE_MESSAGE(
    CmdDependInfoFormatInvalid,
    (msg::value),
    "The alternatives in ``s must not be localized. {value} is what the user specified.",
    "--format={value} is not a recognized format. --format must be one of `list`, `tree`, `mermaid`, `dot`, or `dgml`.")
DECLARE_MESSAGE(CmdDependInfoShowDepthFormatMismatch,
                (),
                "",
                "--show-depth can only be used with `list` and `tree` formats.")
DECLARE_MESSAGE(CmdDependInfoXtreeTree, (), "", "--sort=x-tree cannot be used with formats other than tree")
DECLARE_MESSAGE(CmdDependInfoOptDepth, (), "", "Shows recursion depth in `list` output")
DECLARE_MESSAGE(CmdDependInfoOptMaxRecurse, (), "", "Sets max recursion depth. Default is no limit")
DECLARE_MESSAGE(
    CmdDependInfoOptSort,
    (),
    "The alternatives in ``s must not be localized, but the localized text can explain what each value "
    "means. The value `reverse` means 'reverse-topological'.",
    "Chooses sort order for the `list` format, one of `lexicographical`, `topological` (default), `reverse`")
DECLARE_MESSAGE(CmdDownloadExample1,
                (),
                "This is a command line, only the <filepath> part should be localized",
                "vcpkg x-download <filepath> <sha512> --url=https://...")
DECLARE_MESSAGE(CmdDownloadExample2,
                (),
                "This is a command line, only the <filepath> part should be localized",
                "vcpkg x-download <filepath> --sha512=<sha512> --url=https://...")
DECLARE_MESSAGE(CmdDownloadExample3,
                (),
                "This is a command line, only the <filepath> part should be localized",
                "vcpkg x-download <filepath> --skip-sha512 --url=https://...")
DECLARE_MESSAGE(CmdDownloadSynopsis, (), "", "Downloads a file")
DECLARE_MESSAGE(CmdEditExample1,
                (),
                "This is a command line, only the <port name> part should be localized",
                "vcpkg edit <port name>")
DECLARE_MESSAGE(CmdEditOptAll, (), "", "Opens editor into the port as well as the port-specific buildtree subfolder")
DECLARE_MESSAGE(CmdEditOptBuildTrees, (), "", "Opens editor into the port-specific buildtree subfolder")
DECLARE_MESSAGE(CommandEnvExample2,
                (),
                "This is a command line, only the <path> part should be localized",
                "vcpkg env \"ninja -C <path>\" --triplet x64-windows")
DECLARE_MESSAGE(CmdEnvOptions, (msg::path, msg::env_var), "", "Adds installed {path} to {env_var}")
DECLARE_MESSAGE(CmdExportEmptyPlan,
                (),
                "",
                "Refusing to create an export of zero packages. Install packages before exporting.")
DECLARE_MESSAGE(CmdExportExample1,
                (),
                "This is a command line, only <port names> and the out_dir part should be localized",
                "vcpkg export <port names> [--nuget] [--directory=out_dir]")
DECLARE_MESSAGE(CmdExportOpt7Zip, (), "", "Exports to a 7zip (.7z) file")
DECLARE_MESSAGE(CmdExportOptChocolatey, (), "", "Exports a Chocolatey package (experimental)")
DECLARE_MESSAGE(CmdExportOptDebug, (), "", "Enables prefab debug")
DECLARE_MESSAGE(CmdExportOptDryRun, (), "", "Does not actually export")
DECLARE_MESSAGE(CmdExportOptIFW, (), "", "Exports to an IFW-based installer")
DECLARE_MESSAGE(CmdExportOptInstalled, (), "", "Exports all installed packages")
DECLARE_MESSAGE(CmdExportOptMaven, (), "", "Enables Maven")
DECLARE_MESSAGE(CmdExportOptNuget, (), "", "Exports a NuGet package")
DECLARE_MESSAGE(CmdExportOptPrefab, (), "", "Exports to Prefab format")
DECLARE_MESSAGE(CmdExportOptRaw, (), "", "Exports to an uncompressed directory")
DECLARE_MESSAGE(CmdExportOptZip, (), "", "Exports to a zip file")
DECLARE_MESSAGE(CmdExportSettingChocolateyMaint,
                (),
                "",
                "The maintainer for the exported Chocolatey package (experimental)")
DECLARE_MESSAGE(CmdExportSettingChocolateyVersion,
                (),
                "",
                "The version suffix to add for the exported Chocolatey package (experimental)")
DECLARE_MESSAGE(CmdExportSettingConfigFile, (), "", "The temporary file path for the installer configuration")
DECLARE_MESSAGE(CmdExportSettingInstallerPath, (), "", "The file path for the exported installer")
DECLARE_MESSAGE(CmdExportSettingNugetDesc, (), "", "Description for the exported NuGet package")
DECLARE_MESSAGE(CmdExportSettingNugetID, (), "", "Id for the exported NuGet package (overrides --output)")
DECLARE_MESSAGE(CmdExportSettingNugetVersion, (), "", "The version for the exported NuGet package")
DECLARE_MESSAGE(CmdExportSettingOutput, (), "", "The output name (used to construct filename)")
DECLARE_MESSAGE(CmdExportSettingOutputDir, (), "", "The output directory for produced artifacts")
DECLARE_MESSAGE(CmdExportSettingPkgDir, (), "", "The temporary directory path for the repacked packages")
DECLARE_MESSAGE(CmdExportSettingPrefabArtifactID,
                (),
                "",
                "Artifact Id is the name of the project according Maven specifications")
DECLARE_MESSAGE(CmdExportSettingPrefabGroupID,
                (),
                "",
                "GroupId uniquely identifies your project according Maven specifications")
DECLARE_MESSAGE(CmdExportSettingPrefabVersion,
                (),
                "",
                "Version is the name of the project according Maven specifications")
DECLARE_MESSAGE(CmdExportSettingRepoDir, (), "", "The directory path for the exported repository")
DECLARE_MESSAGE(CmdExportSettingRepoURL, (), "", "The remote repository URL for the online installer")
DECLARE_MESSAGE(CmdExportSettingSDKMinVersion, (), "", "The Android minimum supported SDK version")
DECLARE_MESSAGE(CmdExportSettingSDKTargetVersion, (), "", "The Android target sdk version")
DECLARE_MESSAGE(CmdExportSynopsis, (), "", "Creates a standalone deployment of installed ports")
DECLARE_MESSAGE(CmdFetchOptXStderrStatus,
                (),
                "",
                "Prints status/downloading messages to stderr rather than stdout (Errors/failures still go to stdout)")
DECLARE_MESSAGE(CmdFetchSynopsis, (), "", "Fetches something from the system or the internet")
DECLARE_MESSAGE(CmdFindExample1,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg find port <port name>")
DECLARE_MESSAGE(CmdFindExample2,
                (),
                "This is a command line, only the <>s part should be localized",
                "vcpkg find artifact <artifact name>")
DECLARE_MESSAGE(CmdFindSynopsis, (), "", "Finds a port or artifact that may be installed or activated")
DECLARE_MESSAGE(CmdFormatManifestExample1,
                (),
                "This is a command line, only the <vcpkg.json path>s part should be localized",
                "vcpkg format-manifest <vcpkg.json path>")
DECLARE_MESSAGE(CmdFormatManifestOptAll, (), "", "Formats all ports' manifest files")
DECLARE_MESSAGE(CmdFormatManifestOptConvertControl, (), "", "Converts CONTROL files to manifest files")
DECLARE_MESSAGE(
    CmdGenerateMessageMapOptNoOutputComments,
    (),
    "",
    "Excludes comments when generating the message map (useful for generating the English localization file)")
DECLARE_MESSAGE(CmdFormatManifestSynopsis, (), "", "Prettyfies vcpkg.json")
DECLARE_MESSAGE(CmdGenerateMSBuildPropsExample1,
                (),
                "This is a command line, only the <path> part should be localized",
                "vcpkg generate-msbuild-props --msbuild-props <path>")
DECLARE_MESSAGE(CmdGenerateMSBuildPropsExample2,
                (),
                "This is a command line, only the word 'out' should be localized",
                "vcpkg generate-msbuild-props --msbuild-props out.props")
DECLARE_MESSAGE(
    CmdGenerateMSBuildPropsSynopsis,
    (),
    "",
    "Generates msbuild .props files as if activating a manifest's artifact dependencies, without acquiring them")
DECLARE_MESSAGE(CmdHashExample1,
                (),
                "This is a command line, only the <path> part should be localized",
                "vcpkg hash <path>")
DECLARE_MESSAGE(CmdHashExample2,
                (),
                "This is a command line, only the <path> part should be localized",
                "vcpkg hash <path> SHA256")
DECLARE_MESSAGE(CmdHashSynopsis, (), "", "Gets a file's SHA256 or SHA512")
DECLARE_MESSAGE(CmdHelpCommands, (), "This is a command line, only <command> should be localized.", "help <command>")
DECLARE_MESSAGE(CmdHelpCommandSynopsis, (), "", "Displays help detail for <command>")
DECLARE_MESSAGE(CmdHelpCommandsSynopsis, (), "", "Displays full list of commands, including rare ones not listed here")
DECLARE_MESSAGE(CmdHelpTopic, (), "This is a command line, only <topic> should be localized.", "help <topic>")
DECLARE_MESSAGE(CmdInfoOptInstalled, (), "", "(experimental) Reports installed packages rather than available")
DECLARE_MESSAGE(CmdInfoOptTransitive, (), "", "(experimental) Also reports dependencies of installed packages")
DECLARE_MESSAGE(CmdInitRegistryExample1,
                (),
                "This is a command line, only the <path> part should be localized",
                "vcpkg x-init-registry <path>")
DECLARE_MESSAGE(CmdInitRegistrySynopsis, (), "", "Creates a blank git registry")
DECLARE_MESSAGE(CmdInstallExample1,
                (),
                "This is a command line, only the <> parts should be localized",
                "vcpkg install <port name> <port name>...")
DECLARE_MESSAGE(CmdIntegrateSynopsis, (), "", "Integrates vcpkg with machines, projects, or shells")
DECLARE_MESSAGE(CmdListExample2,
                (),
                "This is a command line, only the <filter> part should be localized",
                "vcpkg list <filter>")
DECLARE_MESSAGE(CmdNewExample1,
                (),
                "This is a command line, only the word example should be localized",
                "vcpkg new --name=example --version=1.0")
DECLARE_MESSAGE(CmdNewOptApplication, (), "", "Creates an application manifest (don't require name or version)")
DECLARE_MESSAGE(CmdNewOptSingleFile, (), "", "Embeds vcpkg-configuration.json into vcpkg.json")
DECLARE_MESSAGE(CmdNewOptVersionDate, (), "", "Interprets --version as an ISO 8601 date. (YYYY-MM-DD)")
DECLARE_MESSAGE(CmdNewOptVersionRelaxed,
                (),
                "",
                "Interprets --version as a relaxed-numeric version (Nonnegative numbers separated by dots)")
DECLARE_MESSAGE(CmdNewOptVersionString, (), "", "Interprets --version as a string with no ordering behavior")
DECLARE_MESSAGE(CmdNewSettingName, (), "", "Name for the new manifest")
DECLARE_MESSAGE(CmdNewSettingVersion, (), "", "Version for the new manifest")
DECLARE_MESSAGE(CmdNewSynposis, (), "", "Creates a new manifest")
DECLARE_MESSAGE(CmdOwnsExample1,
                (),
                "This is a command line, only the part <pattern> should be localized.",
                "vcpkg owns <pattern>")
DECLARE_MESSAGE(CmdPackageInfoExample1,
                (),
                "This is a command line, only the part <package name> should be localized.",
                "vcpkg x-package-info <package name>...")
DECLARE_MESSAGE(CmdPortsdiffExample1,
                (),
                "This is a command line, only the <branch name> part should be localized",
                "vcpkg portsdiff <branch name>")
DECLARE_MESSAGE(CmdPortsdiffExample2,
                (),
                "This is a command line, only the parts in <>s should be localized",
                "vcpkg portsdiff <from> <to>")
DECLARE_MESSAGE(CmdPortsdiffSynopsis, (), "", "Diffs changes in port versions between commits")
DECLARE_MESSAGE(CmdRegenerateOptDryRun, (), "", "Does not actually perform the action, shows only what would be done")
DECLARE_MESSAGE(CmdRegenerateOptForce, (), "", "Proceeds with the (potentially dangerous) action without confirmation")
DECLARE_MESSAGE(CmdRegenerateOptNormalize, (), "", "Applies any deprecation fixes")
DECLARE_MESSAGE(CmdRemoveExample1,
                (),
                "This is a command line, only the part <package name> should be localized.",
                "vcpkg remove <package name>...")
DECLARE_MESSAGE(CmdRemoveOptDryRun, (), "", "Prints the packages to be removed, but does not remove them")
DECLARE_MESSAGE(CmdRemoveOptOutdated,
                (),
                "",
                "Removes all packages with versions that do not match the built-in registry")
DECLARE_MESSAGE(CmdRemoveOptRecurse, (), "", "Allows removal of dependent packages not explicitly specified")
DECLARE_MESSAGE(CmdSearchExample1,
                (),
                "This is a command line, only the part <pattern> should be localized.",
                "vcpkg search <pattern>")
DECLARE_MESSAGE(CmdSettingCopiedFilesLog, (), "", "Path to the copied files log to create")
DECLARE_MESSAGE(CmdSettingInstalledDir, (), "", "Path to the installed tree to use")
DECLARE_MESSAGE(CmdSettingTargetBin, (), "", "Path to the binary to analyze")
DECLARE_MESSAGE(CmdSettingTLogFile, (), "", "Path to the tlog file to create")
DECLARE_MESSAGE(CmdSetInstalledExample1,
                (),
                "This is a command line, only the part <package name> should be localized.",
                "vcpkg x-set-installed <package name> <package name>...")
DECLARE_MESSAGE(CmdSetInstalledOptDryRun, (), "", "Does not actually build or install")
DECLARE_MESSAGE(CmdSetInstalledOptNoUsage, (), "", "Does not print CMake usage information after install")
DECLARE_MESSAGE(CmdSetInstalledOptWritePkgConfig,
                (),
                "'vcpkg help binarycaching' is a command line and should not be localized.",
                "Writes a NuGet packages.config-formatted file for use with external binary caching. "
                "See `vcpkg help binarycaching` for more information")
DECLARE_MESSAGE(CmdSetInstalledSynopsis,
                (),
                "",
                "Installs, upgrades, or removes packages such that that installed matches exactly those supplied")
DECLARE_MESSAGE(CmdUpdateBaselineOptDryRun, (), "", "Prints out plan without execution")
DECLARE_MESSAGE(CmdUpdateBaselineOptInitial,
                (),
                "",
                "Adds a `builtin-baseline` to a vcpkg.json that doesn't already have it")
DECLARE_MESSAGE(CmdUpdateBaselineSynopsis,
                (),
                "",
                "Updates baselines of git registries in a manifest to those registries' HEAD commit")
DECLARE_MESSAGE(CmdUpdateRegistryAll, (), "", "Updates all known artifact registries")
DECLARE_MESSAGE(CmdUpdateRegistryAllExcludesTargets,
                (),
                "",
                "Update registry --all cannot be used with a list of artifact registries")
DECLARE_MESSAGE(CmdUpdateRegistryExample3,
                (),
                "This is a command line, only the <artifact registry name> part should be localized.",
                "vcpkg x-update-registry <artifact registry name>")
DECLARE_MESSAGE(CmdUpdateRegistrySynopsis, (), "", "Re-downloads an artifact registry")
DECLARE_MESSAGE(CmdUpdateRegistryAllOrTargets,
                (),
                "",
                "Update registry requires either a list of artifact registry names or URiIs to update, or --all.")
DECLARE_MESSAGE(CmdUpgradeOptNoDryRun, (), "", "Actually upgrade")
DECLARE_MESSAGE(CmdUpgradeOptNoKeepGoing, (), "", "Stop installing packages on failure")
DECLARE_MESSAGE(CmdUseExample1,
                (),
                "This is a command line, only the <artifact name> part should be localized.",
                "vcpkg use <artifact name>")
DECLARE_MESSAGE(CmdUseSynopsis, (), "", "Activate a single artifact in this shell")
DECLARE_MESSAGE(CmdVSInstancesSynopsis, (), "", "Lists detected Visual Studio instances")
DECLARE_MESSAGE(CmdXDownloadOptHeader, (), "", "Additional header to use when fetching from URLs")
DECLARE_MESSAGE(CmdXDownloadOptSha, (), "", "The hash of the file to be downloaded")
DECLARE_MESSAGE(CmdXDownloadOptSkipSha, (), "", "Skips check of SHA512 of the downloaded file")
DECLARE_MESSAGE(CmdXDownloadOptStore, (), "", "Stores the the file should father than fetching it")
DECLARE_MESSAGE(CmdXDownloadOptUrl, (), "", "URL to download and store if missing from cache")
DECLARE_MESSAGE(
    CmdZApplocalSynopsis,
    (),
    "",
    "Copies a binary's dependencies from the installed tree to where that binary's location for app-local deployment")
DECLARE_MESSAGE(CmdZExtractExample1,
                (),
                "This is a command line, only the parts in <>s should be localized",
                "vcpkg z-extract <archive path> <output directory>")
DECLARE_MESSAGE(CmdZExtractExample2,
                (),
                "This is a command line, the example archive 'source.zip' and the example output directory "
                "'source_dir' should be localized",
                "vcpkg z-extract source.zip source_dir --strip 2")
DECLARE_MESSAGE(CmdZExtractOptStrip, (), "", "The number of leading directories to strip from all paths")
DECLARE_MESSAGE(CommandFailed,
                (msg::command_line),
                "",
                "command:\n"
                "{command_line}\n"
                "failed with the following output:")
DECLARE_MESSAGE(CommandFailedCode,
                (msg::command_line, msg::exit_code),
                "",
                "command:\n"
                "{command_line}\n"
                "failed with exit code {exit_code} and the following output:")
DECLARE_MESSAGE(CommunityTriplets, (), "", "Community Triplets:")
DECLARE_MESSAGE(CompilerPath, (msg::path), "", "Compiler found: {path}")
DECLARE_MESSAGE(CompressFolderFailed, (msg::path), "", "Failed to compress folder \"{path}\":")
DECLARE_MESSAGE(ComputingInstallPlan, (), "", "Computing installation plan...")
DECLARE_MESSAGE(ConfigurationErrorRegistriesWithoutBaseline,
                (msg::path, msg::url),
                "",
                "The configuration defined in {path} is invalid.\n\n"
                "Using registries requires that a baseline is set for the default registry or that the default "
                "registry is null.\n\n"
                "See {url} for more details.")
DECLARE_MESSAGE(ConfigurationNestedDemands,
                (msg::json_field),
                "",
                "[\"{json_field}\"] contains a nested `demands` object (nested `demands` have no effect)")
DECLARE_MESSAGE(ConflictingFiles,
                (msg::path, msg::spec),
                "",
                "The following files are already installed in {path} and are in conflict with {spec}")
DECLARE_MESSAGE(ConsideredVersions,
                (msg::version),
                "",
                "The following executables were considered but discarded because of the version "
                "requirement of {version}:")
DECLARE_MESSAGE(ConstraintViolation, (), "", "Found a constraint violation:")
DECLARE_MESSAGE(ContinueCodeUnitInStart, (), "", "found continue code unit in start position")
DECLARE_MESSAGE(ControlCharacterInString, (), "", "Control character in string")
DECLARE_MESSAGE(ControlSupportsMustBeAPlatformExpression, (), "", "\"Supports\" must be a platform expression")
DECLARE_MESSAGE(CopyrightIsDir, (msg::path), "", "`{path}` being a directory is deprecated.")
DECLARE_MESSAGE(CorruptedDatabase,
                (),
                "",
                "vcpkg's installation database corrupted. This is either a bug in vcpkg or something else has modified "
                "the contents of the 'installed' directory in an unexpected way. You may be able to fix this by "
                "deleting the 'installed' directory and reinstalling what you want to use. If this problem happens "
                "consistently, please file a bug at https://github.com/microsoft/vcpkg .")
DECLARE_MESSAGE(CorruptedInstallTree, (), "", "Your vcpkg 'installed' tree is corrupted.")
DECLARE_MESSAGE(CouldNotDeduceNugetIdAndVersion,
                (msg::path),
                "",
                "Could not deduce nuget id and version from filename: {path}")
DECLARE_MESSAGE(CouldNotFindBaselineInCommit,
                (msg::url, msg::commit_sha, msg::package_name),
                "",
                "Couldn't find baseline in {url} at {commit_sha} for {package_name}.")
DECLARE_MESSAGE(CouldNotFindGitTreeAtCommit,
                (msg::package_name, msg::commit_sha),
                "",
                "could not find the git tree for `versions` in repo {package_name} at commit {commit_sha}")
DECLARE_MESSAGE(CouldNotFindToolVersion,
                (msg::version, msg::path),
                "",
                "Could not find <tools version=\"{version}\"> in {path}")
DECLARE_MESSAGE(CouldNotFindVersionDatabaseFile, (msg::path), "", "Couldn't find the versions database file: {path}")
DECLARE_MESSAGE(CreatedNuGetPackage, (msg::path), "", "Created nupkg: {path}")
DECLARE_MESSAGE(CreateFailureLogsDir, (msg::path), "", "Creating failure logs output directory {path}.")
DECLARE_MESSAGE(Creating7ZipArchive, (), "", "Creating 7zip archive...")
DECLARE_MESSAGE(CreatingNugetPackage, (), "", "Creating NuGet package...")
DECLARE_MESSAGE(CreatingZipArchive, (), "", "Creating zip archive...")
DECLARE_MESSAGE(CreationFailed, (msg::path), "", "Creating {path} failed.")
DECLARE_MESSAGE(CurlFailedToPut,
                (msg::exit_code, msg::url),
                "curl is the name of a program, see curl.se",
                "curl failed to put file to {url} with exit code {exit_code}.")
DECLARE_MESSAGE(CurlFailedToPutHttp,
                (msg::exit_code, msg::url, msg::value),
                "curl is the name of a program, see curl.se. {value} is an HTTP status code",
                "curl failed to put file to {url} with exit code {exit_code} and http code {value}.")
DECLARE_MESSAGE(CurlResponseTruncatedRetrying,
                (msg::value),
                "{value} is the number of milliseconds for which we are waiting this time",
                "curl returned a partial response; waiting {value} milliseconds and trying again")
DECLARE_MESSAGE(CurlTimeout,
                (msg::command_line),
                "",
                "curl was unable to perform all requested HTTP operations, even after timeout and retries. The last "
                "command line was: {command_line}")
DECLARE_MESSAGE(CurrentCommitBaseline,
                (msg::commit_sha),
                "",
                "You can use the current commit as a baseline, which is:\n\t\"builtin-baseline\": \"{commit_sha}\"")
DECLARE_MESSAGE(CycleDetectedDuring, (msg::spec), "", "cycle detected during {spec}:")
DECLARE_MESSAGE(DefaultBinaryCachePlatformCacheRequiresAbsolutePath,
                (msg::path),
                "",
                "Environment variable VCPKG_DEFAULT_BINARY_CACHE must be a directory (was: {path})")
DECLARE_MESSAGE(DefaultBinaryCacheRequiresAbsolutePath,
                (msg::path),
                "",
                "Environment variable VCPKG_DEFAULT_BINARY_CACHE must be absolute (was: {path})")
DECLARE_MESSAGE(DefaultBinaryCacheRequiresDirectory,
                (msg::path),
                "",
                "Environment variable VCPKG_DEFAULT_BINARY_CACHE must be a directory (was: {path})")
DECLARE_MESSAGE(DefaultFeatureCore,
                (),
                "The word \"core\" is an on-disk name that must not be localized.",
                "the feature \"core\" turns off default features and thus can't be in the default features list")
DECLARE_MESSAGE(
    DefaultFeatureDefault,
    (),
    "The word \"default\" is an on-disk name that must not be localized.",
    "the feature \"default\" refers to the set of default features and thus can't be in the default features list")
DECLARE_MESSAGE(DefaultFeatureIdentifier, (), "", "the names of default features must be identifiers")
DECLARE_MESSAGE(DefaultFlag, (msg::option), "", "Defaulting to --{option} being on.")
DECLARE_MESSAGE(DefaultRegistryIsArtifact, (), "", "The default registry cannot be an artifact registry.")
DECLARE_MESSAGE(DeleteVcpkgConfigFromManifest,
                (msg::path),
                "",
                "-- Or remove \"vcpkg-configuration\" from the manifest file {path}.")
DECLARE_MESSAGE(
    DependencyFeatureCore,
    (),
    "The word \"core\" is an on-disk name that must not be localized. The \"default-features\" part is JSON "
    "syntax that must be copied verbatim into the user's file.",
    "the feature \"core\" cannot be in a dependency's feature list. To turn off default features, add "
    "\"default-features\": false instead.")
DECLARE_MESSAGE(
    DependencyFeatureDefault,
    (),
    "The word \"default\" is an on-disk name that must not be localized. The \"default-features\" part is JSON "
    "syntax that must be copied verbatim into the user's file.",
    "the feature \"default\" cannot be in a dependency's feature list. To turn on default features, add "
    "\"default-features\": true instead.")
DECLARE_MESSAGE(DependencyGraphCalculation, (), "", "Dependency graph submission enabled.")
DECLARE_MESSAGE(DependencyGraphFailure, (), "", "Dependency graph submission failed.")
DECLARE_MESSAGE(DependencyGraphSuccess, (), "", "Dependency graph submission successful.")
DECLARE_MESSAGE(DependencyInFeature, (msg::feature), "", "the dependency is in the feature named {feature}")
DECLARE_MESSAGE(DependencyNotInVersionDatabase,
                (msg::package_name),
                "",
                "the dependency {package_name} does not exist in the version database; does that port exist?")
DECLARE_MESSAGE(DeprecatedPrefabDebugOption, (), "", "--prefab-debug is now deprecated.")
DECLARE_MESSAGE(DetectCompilerHash, (msg::triplet), "", "Detecting compiler hash for triplet {triplet}...")
DECLARE_MESSAGE(DocumentedFieldsSuggestUpdate,
                (),
                "",
                "If these are documented fields that should be recognized try updating the vcpkg tool.")
DECLARE_MESSAGE(DownloadAvailable,
                (msg::env_var),
                "",
                "A downloadable copy of this tool is available and can be used by unsetting {env_var}.")
DECLARE_MESSAGE(DownloadedSources, (msg::spec), "", "Downloaded sources for {spec}")
DECLARE_MESSAGE(DownloadFailedCurl,
                (msg::url, msg::exit_code),
                "",
                "{url}: curl failed to download with exit code {exit_code}")
DECLARE_MESSAGE(DownloadFailedHashMismatch,
                (msg::url, msg::path, msg::expected, msg::actual),
                "{expected} and {actual} are SHA512 hashes in hex format.",
                "File does not have the expected hash:\n"
                "url: {url}\n"
                "File: {path}\n"
                "Expected hash: {expected}\n"
                "Actual hash: {actual}")
DECLARE_MESSAGE(DownloadFailedRetrying,
                (msg::value),
                "{value} is a number of milliseconds",
                "Download failed -- retrying after {value}ms")
DECLARE_MESSAGE(DownloadFailedStatusCode,
                (msg::url, msg::value),
                "{value} is an HTTP status code",
                "{url}: failed: status code {value}")
DECLARE_MESSAGE(DownloadingPortableToolVersionX,
                (msg::tool_name, msg::version),
                "",
                "A suitable version of {tool_name} was not found (required v{version}) Downloading "
                "portable {tool_name} {version}...")
DECLARE_MESSAGE(DownloadingTool, (msg::tool_name, msg::url, msg::path), "", "Downloading {tool_name}...\n{url}->{path}")
DECLARE_MESSAGE(DownloadingUrl, (msg::url), "", "Downloading {url}")
DECLARE_MESSAGE(DownloadWinHttpError,
                (msg::system_api, msg::exit_code, msg::url),
                "",
                "{url}: {system_api} failed with exit code {exit_code}")
DECLARE_MESSAGE(DownloadingVcpkgStandaloneBundle, (msg::version), "", "Downloading standalone bundle {version}.")
DECLARE_MESSAGE(DownloadingVcpkgStandaloneBundleLatest, (), "", "Downloading latest standalone bundle.")
DECLARE_MESSAGE(DownloadRootsDir, (msg::env_var), "", "Downloads directory (default: {env_var})")
DECLARE_MESSAGE(DuplicatedKeyInObj,
                (msg::value),
                "{value} is a json property/object",
                "Duplicated key \"{value}\" in an object")
DECLARE_MESSAGE(DuplicatePackagePattern, (msg::package_name), "", "Package \"{package_name}\" is duplicated.")
DECLARE_MESSAGE(DuplicatePackagePatternFirstOcurrence, (), "", "First declared in:")
DECLARE_MESSAGE(DuplicatePackagePatternIgnoredLocations, (), "", "The following redeclarations will be ignored:")
DECLARE_MESSAGE(DuplicatePackagePatternLocation, (msg::path), "", "location: {path}")
DECLARE_MESSAGE(DuplicatePackagePatternRegistry, (msg::url), "", "registry: {url}")
DECLARE_MESSAGE(ElapsedForPackage, (msg::spec, msg::elapsed), "", "Elapsed time to handle {spec}: {elapsed}")
DECLARE_MESSAGE(ElapsedTimeForChecks, (msg::elapsed), "", "Time to determine pass/fail: {elapsed}")
DECLARE_MESSAGE(EmailVcpkgTeam, (msg::url), "", "Send an email to {url} with any feedback.")
DECLARE_MESSAGE(EmbeddingVcpkgConfigInManifest,
                (),
                "",
                "Embedding `vcpkg-configuration` in a manifest file is an EXPERIMENTAL feature.")
DECLARE_MESSAGE(EmptyLicenseExpression, (), "", "SPDX license expression was empty.")
DECLARE_MESSAGE(EndOfStringInCodeUnit, (), "", "found end of string in middle of code point")
DECLARE_MESSAGE(EnvInvalidMaxConcurrency,
                (msg::env_var, msg::value),
                "{value} is the invalid value of an environment variable",
                "{env_var} is {value}, must be > 0")
DECLARE_MESSAGE(EnvStrFailedToExtract, (), "", "could not expand the environment string:")
DECLARE_MESSAGE(EnvPlatformNotSupported, (), "", "Build environment commands are not supported on this platform")
DECLARE_MESSAGE(EnvVarMustBeAbsolutePath, (msg::path, msg::env_var), "", "{env_var} ({path}) was not an absolute path")
DECLARE_MESSAGE(ErrorDetectingCompilerInfo,
                (msg::path),
                "",
                "while detecting compiler information:\nThe log file content at \"{path}\" is:")
DECLARE_MESSAGE(ErrorIndividualPackagesUnsupported,
                (),
                "",
                "In manifest mode, `vcpkg install` does not support individual package arguments.\nTo install "
                "additional packages, edit vcpkg.json and then run `vcpkg install` without any package arguments.")
DECLARE_MESSAGE(ErrorInvalidClassicModeOption,
                (msg::option),
                "",
                "The option --{option} is not supported in classic mode and no manifest was found.")
DECLARE_MESSAGE(ErrorInvalidManifestModeOption,
                (msg::option),
                "",
                "The option --{option} is not supported in manifest mode.")
DECLARE_MESSAGE(
    ErrorMissingVcpkgRoot,
    (),
    "",
    "Could not detect vcpkg-root. If you are trying to use a copy of vcpkg that you've built, you must "
    "define the VCPKG_ROOT environment variable to point to a cloned copy of https://github.com/Microsoft/vcpkg.")
DECLARE_MESSAGE(ErrorNoVSInstance,
                (msg::triplet),
                "",
                "in triplet {triplet}: Unable to find a valid Visual Studio instance")
DECLARE_MESSAGE(ErrorNoVSInstanceAt, (msg::path), "", "at \"{path}\"")
DECLARE_MESSAGE(ErrorNoVSInstanceFullVersion, (msg::version), "", "with toolset version prefix {version}")
DECLARE_MESSAGE(ErrorNoVSInstanceVersion, (msg::version), "", "with toolset version {version}")
DECLARE_MESSAGE(ErrorParsingBinaryParagraph, (msg::spec), "", "while parsing the Binary Paragraph for {spec}")
DECLARE_MESSAGE(ErrorRequireBaseline,
                (),
                "",
                "this vcpkg instance requires a manifest with a specified baseline in order to "
                "interact with ports. Please add 'builtin-baseline' to the manifest or add a "
                "'vcpkg-configuration.json' that redefines the default registry.")
DECLARE_MESSAGE(ErrorRequirePackagesList,
                (),
                "",
                "`vcpkg install` requires a list of packages to install in classic mode.")
DECLARE_MESSAGE(ErrorInvalidExtractOption,
                (msg::option, msg::value),
                "The keyword 'AUTO' should not be localized",
                "--{option} must be set to a nonnegative integer or 'AUTO'.")
DECLARE_MESSAGE(ErrorUnableToDetectCompilerInfo,
                (),
                "failure output will be displayed at the top of this",
                "vcpkg was unable to detect the active compiler's information. See above for the CMake failure output.")
DECLARE_MESSAGE(ErrorVcvarsUnsupported,
                (msg::triplet),
                "",
                "in triplet {triplet}: Use of Visual Studio's Developer Prompt is unsupported "
                "on non-Windows hosts.\nDefine 'VCPKG_CMAKE_SYSTEM_NAME' or "
                "'VCPKG_CHAINLOAD_TOOLCHAIN_FILE' in the triplet file.")
DECLARE_MESSAGE(ErrorVsCodeNotFound,
                (msg::env_var),
                "",
                "Visual Studio Code was not found and the environment variable {env_var} is not set or invalid.")
DECLARE_MESSAGE(ErrorVsCodeNotFoundPathExamined, (), "", "The following paths were examined:")
DECLARE_MESSAGE(ErrorWhileFetchingBaseline,
                (msg::value, msg::package_name),
                "{value} is a commit sha.",
                "while fetching baseline `\"{value}\"` from repo {package_name}:")
DECLARE_MESSAGE(ErrorWhileParsing, (msg::path), "", "Errors occurred while parsing {path}.")
DECLARE_MESSAGE(ErrorWhileWriting, (msg::path), "", "Error occurred while writing {path}.")
DECLARE_MESSAGE(ExamplesHeader, (), "Printed before a list of example command lines", "Examples:")
DECLARE_MESSAGE(ExceededRecursionDepth, (), "", "Recursion depth exceeded.")
DECLARE_MESSAGE(ExcludedPackage, (msg::spec), "", "Excluded {spec}")
DECLARE_MESSAGE(ExcludedPackages, (), "", "The following packages are excluded:")
DECLARE_MESSAGE(ExpectedAnObject, (), "", "expected an object")
DECLARE_MESSAGE(ExpectedAtMostOneSetOfTags,
                (msg::count, msg::old_value, msg::new_value, msg::value),
                "{old_value} is a left tag and {new_value} is the right tag. {value} is the input.",
                "Found {count} sets of {old_value}.*{new_value} but expected at most 1, in block:\n{value}")
DECLARE_MESSAGE(ExpectedCharacterHere,
                (msg::expected),
                "{expected} is a locale-invariant delimiter; for example, the ':' or '=' in 'zlib:x64-windows=skip'",
                "expected '{expected}' here")
DECLARE_MESSAGE(ExpectedDefaultFeaturesList, (), "", "expected ',' or end of text in default features list")
DECLARE_MESSAGE(ExpectedDependenciesList, (), "", "expected ',' or end of text in dependencies list")
DECLARE_MESSAGE(ExpectedDigitsAfterDecimal, (), "", "Expected digits after the decimal point")
DECLARE_MESSAGE(ExpectedFailOrSkip, (), "", "expected 'fail', 'skip', or 'pass' here")
DECLARE_MESSAGE(ExpectedFeatureListTerminal, (), "", "expected ',' or ']' in feature list")
DECLARE_MESSAGE(ExpectedFeatureName, (), "", "expected feature name (must be lowercase, digits, '-')")
DECLARE_MESSAGE(ExpectedExplicitTriplet, (), "", "expected an explicit triplet")
DECLARE_MESSAGE(ExpectedOneSetOfTags,
                (msg::count, msg::old_value, msg::new_value, msg::value),
                "{old_value} is a left tag and {new_value} is the right tag. {value} is the input.",
                "Found {count} sets of {old_value}.*{new_value} but expected exactly 1, in block:\n{value}")
DECLARE_MESSAGE(ExpectedOneVersioningField, (), "", "expected only one versioning field")
DECLARE_MESSAGE(ExpectedPathToExist, (msg::path), "", "Expected {path} to exist after fetching")
DECLARE_MESSAGE(ExpectedPortName, (), "", "expected a port name here (must be lowercase, digits, '-')")
DECLARE_MESSAGE(ExpectedReadWriteReadWrite, (), "", "unexpected argument: expected 'read', readwrite', or 'write'")
DECLARE_MESSAGE(ExpectedStatusField, (), "", "Expected 'status' field in status paragraph")
DECLARE_MESSAGE(ExpectedTripletName, (), "", "expected a triplet name here (must be lowercase, digits, '-')")
DECLARE_MESSAGE(ExportArchitectureReq,
                (),
                "",
                "Export prefab requires targeting at least one of the following architectures arm64-v8a, "
                "armeabi-v7a, x86_64, x86 to be present.")
DECLARE_MESSAGE(Exported7zipArchive, (msg::path), "", "7zip archive exported at: {path}")
DECLARE_MESSAGE(ExportedZipArchive, (msg::path), "", "Zip archive exported at: {path}")
DECLARE_MESSAGE(ExportingAlreadyBuiltPackages, (), "", "The following packages are already built and will be exported:")
DECLARE_MESSAGE(ExportingMaintenanceTool, (), "", "Exporting maintenance tool...")
DECLARE_MESSAGE(ExportingPackage, (msg::package_name), "", "Exporting {package_name}...")
DECLARE_MESSAGE(ExportPrefabRequiresAndroidTriplet, (), "", "export prefab requires an Android triplet.")
DECLARE_MESSAGE(ExtendedDocumentationAtUrl, (msg::url), "", "Extended documentation available at '{url}'.")
DECLARE_MESSAGE(ExtractHelp, (), "", "Extracts an archive.")
DECLARE_MESSAGE(ExtractingTool, (msg::tool_name), "", "Extracting {tool_name}...")
DECLARE_MESSAGE(FailedPostBuildChecks,
                (msg::count, msg::path),
                "",
                "Found {count} post-build check problem(s). To submit these ports to curated catalogs, please "
                "first correct the portfile: {path}")
DECLARE_MESSAGE(FailedToAcquireMutant,
                (msg::path),
                "'mutant' is the Windows kernel object returned by CreateMutexW",
                "failed to acquire mutant {path}")
DECLARE_MESSAGE(FailedToCheckoutRepo,
                (msg::package_name),
                "",
                "failed to check out `versions` from repo {package_name}")
DECLARE_MESSAGE(FailedToDeleteDueToFile,
                (msg::value, msg::path),
                "{value} is the parent path of {path} we tried to delete; the underlying Windows error message is "
                "printed after this",
                "failed to remove_all({value}) due to {path}: ")
DECLARE_MESSAGE(FailedToDeleteInsideDueToFile,
                (msg::value, msg::path),
                "{value} is the parent path of {path} we tried to delete; the underlying Windows error message is "
                "printed after this",
                "failed to remove_all_inside({value}) due to {path}: ")
DECLARE_MESSAGE(FailedToDetermineCurrentCommit, (), "", "Failed to determine the current commit:")
DECLARE_MESSAGE(FailedToDownloadFromMirrorSet, (), "", "Failed to download from mirror set")
DECLARE_MESSAGE(FailedToExtract, (msg::path), "", "Failed to extract \"{path}\":")
DECLARE_MESSAGE(FailedToFetchRepo, (msg::url), "", "Failed to fetch {url}.")
DECLARE_MESSAGE(FailedToFindPortFeature,
                (msg::feature, msg::package_name),
                "",
                "{package_name} has no feature named {feature}.")
DECLARE_MESSAGE(FailedToFormatMissingFile,
                (),
                "",
                "No files to format.\nPlease pass either --all, or the explicit files to format or convert.")
DECLARE_MESSAGE(
    FailedToLoadInstalledManifest,
    (msg::package_name),
    "",
    "The control or manifest file for {package_name} could not be loaded due to the following error. Please "
    "remove {package_name} and try again.")
DECLARE_MESSAGE(FailedToLoadManifest, (msg::path), "", "Failed to load manifest from directory {path}")
DECLARE_MESSAGE(FailedToLocateSpec, (msg::spec), "", "Failed to locate spec in graph: {spec}")
DECLARE_MESSAGE(FailedToObtainDependencyVersion, (), "", "Cannot find desired dependency version.")
DECLARE_MESSAGE(FailedToObtainPackageVersion, (), "", "Cannot find desired package version.")
DECLARE_MESSAGE(FailedToOpenAlgorithm,
                (msg::value),
                "{value} is a crypto algorithm like SHA-1 or SHA-512",
                "failed to open {value}")
DECLARE_MESSAGE(FailedToParseCMakeConsoleOut,
                (),
                "",
                "Failed to parse CMake console output to locate block start/end markers.")
DECLARE_MESSAGE(FailedToParseBaseline, (msg::path), "", "Failed to parse baseline: {path}")
DECLARE_MESSAGE(FailedToParseConfig, (msg::path), "", "Failed to parse configuration: {path}")
DECLARE_MESSAGE(FailedToParseControl, (msg::path), "", "Failed to parse CONTROL file: {path}")
DECLARE_MESSAGE(FailedToParseManifest, (msg::path), "", "Failed to parse manifest file: {path}")
DECLARE_MESSAGE(FailedToParseVersionFile, (msg::path), "", "Failed to parse version file: {path}")
DECLARE_MESSAGE(FailedToParseNoTopLevelObj, (msg::path), "", "Failed to parse {path}, expected a top-level object.")
DECLARE_MESSAGE(FailedToParseNoVersionsArray, (msg::path), "", "Failed to parse {path}, expected a 'versions' array.")
DECLARE_MESSAGE(FailedToParseSerializedBinParagraph,
                (msg::error_msg),
                "'{error_msg}' is the error message for failing to parse the Binary Paragraph.",
                "[sanity check] Failed to parse a serialized binary paragraph.\nPlease open an issue at "
                "https://github.com/microsoft/vcpkg, "
                "with the following output:\n{error_msg}\nSerialized Binary Paragraph:")
DECLARE_MESSAGE(FailedToParseVersionXML,
                (msg::tool_name, msg::version),
                "",
                "Could not parse version for tool {tool_name}. Version string was: {version}")
DECLARE_MESSAGE(FailedToReadParagraph, (msg::path), "", "Failed to read paragraphs from {path}")
DECLARE_MESSAGE(FailedToRunToolToDetermineVersion,
                (msg::tool_name, msg::path),
                "Additional information, such as the command line output, if any, will be appended on "
                "the line after this message",
                "Failed to run \"{path}\" to determine the {tool_name} version.")
DECLARE_MESSAGE(FailedToStoreBackToMirror, (), "", "failed to store back to mirror:")
DECLARE_MESSAGE(FailedToStoreBinaryCache, (msg::path), "", "Failed to store binary cache {path}")
DECLARE_MESSAGE(FailedToTakeFileSystemLock, (), "", "Failed to take the filesystem lock")
DECLARE_MESSAGE(FailedVendorAuthentication,
                (msg::vendor, msg::url),
                "",
                "One or more {vendor} credential providers failed to authenticate. See '{url}' for more details "
                "on how to provide credentials.")
DECLARE_MESSAGE(FileIsNotExecutable, (), "", "this file does not appear to be executable")
DECLARE_MESSAGE(FilesContainAbsolutePath1,
                (),
                "This message is printed before a list of found absolute paths, followed by FilesContainAbsolutePath2, "
                "followed by a list of found files.",
                "There should be no absolute paths, such as the following, in an installed package:")
DECLARE_MESSAGE(FilesContainAbsolutePath2, (), "", "Absolute paths were found in the following files:")
DECLARE_MESSAGE(FindVersionArtifactsOnly,
                (),
                "'--version', 'vcpkg search', and 'vcpkg find port' are command lines that must not be localized",
                "--version can't be used with vcpkg search or vcpkg find port")
DECLARE_MESSAGE(FieldKindDidNotHaveExpectedValue,
                (msg::expected, msg::actual),
                "{expected} is a list of literal kinds the user must type, separated by commas, {actual} is what "
                "the user supplied",
                "\"kind\" did not have an expected value: (expected one of: {expected}; found {actual})")
DECLARE_MESSAGE(FetchingBaselineInfo, (msg::package_name), "", "Fetching baseline information from {package_name}...")
DECLARE_MESSAGE(FetchingRegistryInfo,
                (msg::url, msg::value),
                "{value} is a reference",
                "Fetching registry information from {url} ({value})...")
DECLARE_MESSAGE(FileNotFound, (msg::path), "", "{path}: file not found")
DECLARE_MESSAGE(FileReadFailed,
                (msg::path, msg::byte_offset, msg::count),
                "",
                "Failed to read {count} bytes from {path} at offset {byte_offset}.")
DECLARE_MESSAGE(FileSeekFailed,
                (msg::path, msg::byte_offset),
                "",
                "Failed to seek to position {byte_offset} in {path}.")
DECLARE_MESSAGE(FilesExported, (msg::path), "", "Files exported at: {path}")
DECLARE_MESSAGE(FishCompletion, (msg::path), "", "vcpkg fish completion is already added at \"{path}\".")
DECLARE_MESSAGE(FloatingPointConstTooBig, (msg::count), "", "Floating point constant too big: {count}")
DECLARE_MESSAGE(FollowingPackagesMissingControl,
                (),
                "",
                "The following packages do not have a valid CONTROL or vcpkg.json:")
DECLARE_MESSAGE(FollowingPackagesNotInstalled, (), "", "The following packages are not installed:")
DECLARE_MESSAGE(FollowingPackagesUpgraded, (), "", "The following packages are up-to-date:")
DECLARE_MESSAGE(
    ForceSystemBinariesOnWeirdPlatforms,
    (),
    "",
    "Environment variable VCPKG_FORCE_SYSTEM_BINARIES must be set on arm, s390x, ppc64le and riscv platforms.")
DECLARE_MESSAGE(FormattedParseMessageExpressionPrefix, (), "", "on expression:")
DECLARE_MESSAGE(ForMoreHelp,
                (),
                "Printed before a suggestion for the user to run `vcpkg help <topic>`",
                "For More Help")
DECLARE_MESSAGE(GeneratedConfiguration, (msg::path), "", "Generated configuration {path}.")
DECLARE_MESSAGE(GeneratedInstaller, (msg::path), "", "{path} installer generated.")
DECLARE_MESSAGE(GeneratingConfiguration, (msg::path), "", "Generating configuration {path}...")
DECLARE_MESSAGE(GeneratingInstaller, (msg::path), "", "Generating installer {path}...")
DECLARE_MESSAGE(GeneratingRepo, (msg::path), "", "Generating repository {path}...")
DECLARE_MESSAGE(GetParseFailureInfo, (), "", "Use '--debug' to get more information about the parse failures.")
DECLARE_MESSAGE(GHAParametersMissing,
                (msg::url),
                "",
                "The GHA binary source requires the ACTIONS_RUNTIME_TOKEN and ACTIONS_CACHE_URL environment variables "
                "to be set. See {url} for details.")
DECLARE_MESSAGE(GitCommandFailed, (msg::command_line), "", "failed to execute: {command_line}")
DECLARE_MESSAGE(GitCommitUpdateVersionDatabase,
                (),
                "This is a command line; only the 'update version database' part should be localized",
                "git commit -m \"Update version database\"")
DECLARE_MESSAGE(GitFailedToFetch,
                (msg::value, msg::url),
                "{value} is a git ref like 'origin/main'",
                "failed to fetch ref {value} from repository {url}")
DECLARE_MESSAGE(GitFailedToInitializeLocalRepository, (msg::path), "", "failed to initialize local repository {path}")
DECLARE_MESSAGE(
    GitRegistryMustHaveBaseline,
    (msg::url, msg::commit_sha),
    "",
    "The git registry \"{url}\" must have a \"baseline\" field that is a valid git commit SHA (40 hexadecimal "
    "characters).\nTo use the current latest versions, set baseline to that repo's HEAD, \"{commit_sha}\".")
DECLARE_MESSAGE(GitStatusOutputExpectedFileName, (), "", "expected a file name")
DECLARE_MESSAGE(GitStatusOutputExpectedNewLine, (), "", "expected new line")
DECLARE_MESSAGE(GitStatusOutputExpectedRenameOrNewline, (), "", "expected renamed file or new lines")
DECLARE_MESSAGE(GitStatusUnknownFileStatus,
                (msg::value),
                "{value} is a single character indicating file status, for example: A, U, M, D",
                "unknown file status: {value}")
DECLARE_MESSAGE(GitUnexpectedCommandOutputCmd,
                (msg::command_line),
                "",
                "git produced unexpected output when running {command_line}")
DECLARE_MESSAGE(GraphCycleDetected,
                (msg::package_name),
                "A list of package names comprising the cycle will be printed after this message.",
                "Cycle detected within graph at {package_name}:")
DECLARE_MESSAGE(HashFileFailureToRead,
                (msg::path),
                "Printed after ErrorMessage and before the specific failing filesystem operation (like file not found)",
                "failed to read file \"{path}\" for hashing: ")
DECLARE_MESSAGE(HashPortManyFiles,
                (msg::package_name, msg::count),
                "",
                "The {package_name} contains {count} files. Hashing these contents may take a long time when "
                "determining the ABI hash for binary caching. Consider reducing the number of files. Common causes of "
                "this are accidentally checking out source or build files into a port's directory.")
DECLARE_MESSAGE(HeaderOnlyUsage,
                (msg::package_name),
                "'header' refers to C/C++ .h files",
                "{package_name} is header-only and can be used from CMake via:")
DECLARE_MESSAGE(
    HelpAssetCaching,
    (),
    "The '<rw>' part references code in the following table and should not be localized. The matching values "
    "\"read\" \"write\" and \"readwrite\" are also fixed. After this block a table with each possible asset "
    "caching source is printed.",
    "**Experimental feature: this may change or be removed at any time**\n"
    "\n"
    "vcpkg can use mirrors to cache downloaded assets, ensuring continued operation even if the "
    "original source changes or disappears.\n"
    "\n"
    "Asset caching can be configured either by setting the environment variable X_VCPKG_ASSET_SOURCES "
    "to a semicolon-delimited list of sources or by passing a sequence of "
    "--x-asset-sources=<source> command line options. Command line sources are interpreted after "
    "environment sources. Commas, semicolons, and backticks can be escaped using backtick (`).\n"
    "\n"
    "The <rw> optional parameter for certain strings controls how they will be accessed. It can be specified as "
    "\"read\", \"write\", or \"readwrite\" and defaults to \"read\".\n"
    "\n"
    "Valid sources:")
DECLARE_MESSAGE(
    HelpAssetCachingAzUrl,
    (),
    "This is printed as the 'definition' in a table for 'x-azurl,<url>[,<sas>[,<rw>]]', so <url>, <sas>, and <rw> "
    "should not be localized.",
    "Adds an Azure Blob Storage source, optionally using Shared Access Signature validation. URL should include "
    "the container path and be terminated with a trailing \"/\". <sas>, if defined, should be prefixed with a "
    "\"?\". "
    "Non-Azure servers will also work if they respond to GET and PUT requests of the form: "
    "\"<url><sha512><sas>\".")
DECLARE_MESSAGE(HelpAssetCachingBlockOrigin,
                (),
                "This is printed as the 'definition' in a table for 'x-block-origin'",
                "Disables fallback to the original URLs in case the mirror does not have the file available.")
DECLARE_MESSAGE(
    HelpAssetCachingScript,
    (),
    "This is printed as the 'definition' in a table for 'x-script,<template>', so <template> should not be "
    "localized.",
    "Dispatches to an external tool to fetch the asset. Within the template, \"{{url}}\" will be replaced by the "
    "original url, \"{{sha512}}\" will be replaced by the SHA512 value, and \"{{dst}}\" will be replaced by the "
    "output path to save to. These substitutions will all be properly shell escaped, so an example template would "
    "be: \"curl -L {{url}} --output {{dst}}\". \"{{{{\" will be replaced by \"}}\" and \"}}}}\" will be replaced "
    "by \"}}\" to avoid expansion. Note that this will be executed inside the build environment, so the PATH and "
    "other environment variables will be modified by the triplet.")
DECLARE_MESSAGE(
    HelpBinaryCaching,
    (),
    "The names in angle brackets like <rw> or in curly braces like {{sha512}} are 'code' and should not be "
    "localized. The matching values \"read\" \"write\" and \"readwrite\" are also fixed.",
    "vcpkg can cache compiled packages to accelerate restoration on a single machine or across the network. By "
    "default, vcpkg will save builds to a local machine cache. This can be disabled by passing "
    "\"--binarysource=clear\" as the last option on the command line.\n"
    "\n"
    "Binary caching can be further configured by either passing \"--binarysource=<source>\" options to every "
    "command line or setting the `VCPKG_BINARY_SOURCES` environment variable to a set of sources (Example: "
    "\"<source>;<source>;...\"). Command line sources are interpreted after environment sources.\n"
    "\n"
    "The \"<rw>\" optional parameter for certain strings controls whether they will be consulted for downloading "
    "binaries and whether on-demand builds will be uploaded to that remote. It can be specified as \"read\", "
    "\"write\", or \"readwrite\".\n"
    "\n"
    "General sources:")
DECLARE_MESSAGE(HelpBinaryCachingAws,
                (),
                "Printed as the 'definition' for 'x-aws,<prefix>[,<rw>]', so '<prefix>' must be preserved verbatim.",
                "**Experimental: will change or be removed without warning**\n"
                "Adds an AWS S3 source. Uses the aws CLI for uploads and downloads. Prefix should include s3:// "
                "scheme and be suffixed with a \"/\".")
DECLARE_MESSAGE(HelpBinaryCachingAwsConfig,
                (),
                "Printed as the 'definition' for 'x-aws-config,<parameter>'.",
                "**Experimental: will change or be removed without warning**\n"
                "Adds an AWS S3 source. Adds an AWS configuration; currently supports only 'no-sign-request' "
                "parameter that is an equivalent to the --no-sign-request parameter "
                "of the AWS CLI.")
DECLARE_MESSAGE(HelpBinaryCachingAwsHeader, (), "", "Azure Web Services sources")
DECLARE_MESSAGE(HelpBinaryCachingAzBlob,
                (),
                "Printed as the 'definition' for 'x-azblob,<url>,<sas>[,<rw>]'.",
                "**Experimental: will change or be removed without warning**\n"
                "Adds an Azure Blob Storage source. Uses Shared Access Signature validation. <url> should include "
                "the container path. <sas> must be be prefixed with a \"?\".")
DECLARE_MESSAGE(HelpBinaryCachingCos,
                (),
                "Printed as the 'definition' for 'x-cos,<prefix>[,<rw>]'.",
                "**Experimental: will change or be removed without warning**\n"
                "Adds an COS source. Uses the cos CLI for uploads and downloads. <prefix> should include the "
                "scheme 'cos://' and be suffixed with a \"/\".")
DECLARE_MESSAGE(HelpBinaryCachingDefaults,
                (msg::path),
                "Printed as the 'definition' in a table for 'default[,<rw>]'. %LOCALAPPDATA%, %APPDATA%, "
                "$XDG_CACHE_HOME, and $HOME are 'code' and should not be localized.",
                "Adds the default file-based location. Based on your system settings, the default path to store "
                "binaries is \"{path}\". This consults %LOCALAPPDATA%/%APPDATA% on Windows and $XDG_CACHE_HOME or "
                "$HOME on other platforms.")
DECLARE_MESSAGE(HelpBinaryCachingDefaultsError,
                (),
                "Printed as the 'definition' in a table for 'default[,<rw>]', when there was an error fetching the "
                "default for some reason.",
                "Adds the default file-based location.")
DECLARE_MESSAGE(HelpBinaryCachingFiles,
                (),
                "Printed as the 'definition' for 'files,<path>[,<rw>]'",
                "Adds a custom file-based location.")
DECLARE_MESSAGE(HelpBinaryCachingGcs,
                (),
                "Printed as the 'definition' for 'x-gcs,<prefix>[,<rw>]'.",
                "**Experimental: will change or be removed without warning**\n"
                "Adds a Google Cloud Storage (GCS) source. Uses the gsutil CLI for uploads and downloads. Prefix "
                "should include the gs:// scheme and be suffixed with a \"/\".")
DECLARE_MESSAGE(
    HelpBinaryCachingHttp,
    (),
    "Printed as the 'definition' of 'http,<url_template>[,<rw>[,<header>]]', so <url_template>, <rw> and <header> "
    "must be unlocalized. GET, HEAD, and PUT are HTTP verbs that should be not changed. Entries in {{curly "
    "braces}} also must be unlocalized.",
    "Adds a custom http-based location. GET, HEAD and PUT request are done to download, check and upload the "
    "binaries. You can use the variables {{name}}, {{version}}, {{sha}} and {{triplet}}. An example url would be"
    "'https://cache.example.com/{{triplet}}/{{name}}/{{version}}/{{sha}}'. Via the header field you can set a "
    "custom header to pass an authorization token.")
DECLARE_MESSAGE(HelpBinaryCachingNuGet,
                (),
                "Printed as the 'definition' of 'nuget,<uri>[,<rw>]'.",
                "Adds a NuGet-based source; equivalent to the \"-Source\" parameter of the NuGet CLI.")
DECLARE_MESSAGE(HelpBinaryCachingNuGetConfig,
                (),
                "Printed as the 'definition' of 'nugetconfig,<path>[,<rw>]'.",
                "Adds a NuGet-config-file-based source; equivalent to the \"-Config\" parameter of the NuGet CLI. "
                "This config should specify \"defaultPushSource\" for uploads.")
DECLARE_MESSAGE(HelpBinaryCachingNuGetHeader, (), "", "NuGet sources")
DECLARE_MESSAGE(HelpBinaryCachingNuGetInteractive,
                (),
                "Printed as the 'definition' of 'interactive'.",
                "Enables NuGet interactive credential management; the opposite of the \"-NonInteractive\" "
                "parameter in the NuGet CLI.")
DECLARE_MESSAGE(HelpBinaryCachingNuGetFooter,
                (),
                "Printed after the 'nuget', 'nugetconfig', 'nugettimeout', and 'interactive' entries; those names "
                "must not be localized. Printed before an example XML snippet vcpkg generates when the indicated "
                "environment variables are set.",
                "NuGet's cache is not used by default. To use it for every NuGet-based source, set the environment "
                "variable \"VCPKG_USE_NUGET_CACHE\" to \"true\" (case-insensitive) or \"1\".\n"
                "The \"nuget\" and \"nugetconfig\" source providers respect certain environment variables while "
                "generating NuGet packages. If the appropriate environment variables are defined and non-empty, "
                "\"metadata.repository\" field will be generated like one of the following examples:")
DECLARE_MESSAGE(HelpBinaryCachingNuGetTimeout,
                (),
                "Printed as the 'definition' of 'nugettimeout,<seconds>'",
                "Specifies a NuGet timeout for NuGet network operations; equivalent to the \"-Timeout\" parameter "
                "of the NuGet CLI.")
DECLARE_MESSAGE(HelpBuiltinBase,
                (),
                "",
                "The baseline references a commit within the vcpkg repository that establishes a minimum version on "
                "every dependency in the graph. For example, if no other constraints are specified (directly or "
                "transitively), then the version will resolve to the baseline of the top level manifest. Baselines "
                "of transitive dependencies are ignored.")
DECLARE_MESSAGE(HelpCachingClear, (), "", "Removes all previous sources, including defaults.")
DECLARE_MESSAGE(HelpContactCommand, (), "", "Displays contact information to send feedback")
DECLARE_MESSAGE(HelpCreateCommand, (), "", "Creates a new port")
DECLARE_MESSAGE(HelpDependInfoCommand, (), "", "Displays a list of dependencies for ports")
DECLARE_MESSAGE(HelpEditCommand,
                (msg::env_var),
                "\"code\" is the name of a program and should not be localized.",
                "Edits a port, optionally with {env_var}, defaults to \"code\"")
DECLARE_MESSAGE(HelpEnvCommand, (), "", "Creates a clean shell environment for development or compiling")
DECLARE_MESSAGE(HelpExampleCommand, (), "", "For more help (including examples) see https://learn.microsoft.com/vcpkg")
DECLARE_MESSAGE(HelpExampleManifest, (), "", "Example manifest:")
DECLARE_MESSAGE(HelpInstallCommand, (), "", "Installs a package")
DECLARE_MESSAGE(HelpManifestConstraints,
                (),
                "",
                "Manifests can place three kinds of constraints upon the versions used")
DECLARE_MESSAGE(
    HelpMinVersion,
    (),
    "",
    "Vcpkg will select the minimum version found that matches all applicable constraints, including the "
    "version from the baseline specified at top-level as well as any \"version>=\" constraints in the graph.")
DECLARE_MESSAGE(
    HelpOverrides,
    (),
    "",
    "When used as the top-level manifest (such as when running `vcpkg install` in the directory), overrides "
    "allow a manifest to short-circuit dependency resolution and specify exactly the version to use. These can "
    "be used to handle version conflicts, such as with `version-string` dependencies. They will not be "
    "considered when transitively depended upon.")
DECLARE_MESSAGE(HelpOwnsCommand, (), "", "Searches for the owner of a file in installed packages")
DECLARE_MESSAGE(
    HelpPackagePublisher,
    (),
    "",
    "Additionally, package publishers can use \"version>=\" constraints to ensure that consumers are using at "
    "least a certain minimum version of a given dependency. For example, if a library needs an API added "
    "to boost-asio in 1.70, a \"version>=\" constraint will ensure transitive users use a sufficient version "
    "even in the face of individual version overrides or cross-registry references.")
DECLARE_MESSAGE(
    HelpPortVersionScheme,
    (),
    "",
    "Each version additionally has a \"port-version\" which is a nonnegative integer. When rendered as "
    "text, the port version (if nonzero) is added as a suffix to the primary version text separated by a "
    "hash (#). Port-versions are sorted lexicographically after the primary version text, for example:\n1.0.0 < "
    "1.0.0#1 < 1.0.1 < 1.0.1#5 < 2.0.0")
DECLARE_MESSAGE(HelpRemoveCommand, (), "", "Uninstalls a package")
DECLARE_MESSAGE(HelpResponseFileCommand,
                (),
                "Describing what @response_file does on vcpkg's command line",
                "Contains one argument per line expanded at that location")
DECLARE_MESSAGE(HelpSearchCommand, (), "", "Searches for packages available to be built")
DECLARE_MESSAGE(HelpTextOptFullDesc, (), "", "Does not truncate long text")
DECLARE_MESSAGE(HelpTopicCommand, (), "", "Displays specific help topic")
DECLARE_MESSAGE(HelpTopicsCommand, (), "", "Displays full list of help topics")
DECLARE_MESSAGE(HelpTxtOptAllowUnsupportedPort,
                (),
                "",
                "Continues with a warning on unsupported ports, rather than failing")
DECLARE_MESSAGE(HelpTxtOptCleanAfterBuild,
                (),
                "",
                "Cleans buildtrees, packages and downloads after building each package")
DECLARE_MESSAGE(HelpTxtOptCleanBuildTreesAfterBuild, (), "", "Cleans buildtrees after building each package")
DECLARE_MESSAGE(HelpTxtOptCleanDownloadsAfterBuild, (), "", "Cleans downloads after building each package")
DECLARE_MESSAGE(HelpTxtOptCleanPkgAfterBuild, (), "", "Cleans packages after building each package")
DECLARE_MESSAGE(HelpTxtOptDryRun, (), "", "Does not actually build or install")
DECLARE_MESSAGE(HelpTxtOptEditable,
                (),
                "",
                "Disables source re-extraction and binary caching for libraries on the command line (classic mode)")
DECLARE_MESSAGE(HelpTxtOptEnforcePortChecks,
                (),
                "",
                "Fails install if a port has detected problems or attempts to use a deprecated feature")
DECLARE_MESSAGE(HelpTxtOptKeepGoing, (), "", "Continues installing packages on failure")
DECLARE_MESSAGE(HelpTxtOptManifestFeature,
                (),
                "",
                "Additional features from the top-level manifest to install (manifest mode)")
DECLARE_MESSAGE(HelpTxtOptManifestNoDefault,
                (),
                "",
                "Does not install the default features from the top-level manifest (manifest mode)")
DECLARE_MESSAGE(HelpTxtOptNoDownloads, (), "", "Does not download new sources")
DECLARE_MESSAGE(HelpTxtOptNoUsage, (), "", "Does not print CMake usage information after install")
DECLARE_MESSAGE(HelpTxtOptOnlyBinCache, (), "", "Fails if cached binaries are not available")
DECLARE_MESSAGE(HelpTxtOptOnlyDownloads, (), "", "Makes best-effort attempt to download sources without building")
DECLARE_MESSAGE(HelpTxtOptRecurse, (), "", "Allows removal of packages as part of installation")
DECLARE_MESSAGE(HelpTxtOptUseAria2, (), "", "Uses aria2 to perform download tasks")
DECLARE_MESSAGE(HelpTxtOptUseHeadVersion,
                (),
                "",
                "Installs the libraries on the command line using the latest upstream sources (classic mode)")
DECLARE_MESSAGE(HelpTxtOptWritePkgConfig,
                (),
                "'vcpkg help binarycaching' is a command line and should not be localized.",
                "Writes a NuGet packages.config-formatted file for use with external binary caching. See `vcpkg help "
                "binarycaching` for more information")
DECLARE_MESSAGE(HelpUpdateBaseline,
                (),
                "",
                "The best approach to keep your libraries up to date is to update your baseline reference. This will "
                "ensure all packages, including transitive ones, are updated. However if you need to update a package "
                "independently, you can use a \"version>=\" constraint.")
DECLARE_MESSAGE(HelpUpdateCommand, (), "", "Lists packages that can be upgraded")
DECLARE_MESSAGE(HelpUpgradeCommand, (), "", "Rebuilds all outdated packages")
DECLARE_MESSAGE(HelpVersionCommand, (), "", "Displays version information")
DECLARE_MESSAGE(HelpVersionDateScheme, (), "", "A date (2021-01-01.5)")
DECLARE_MESSAGE(HelpVersionGreater,
                (),
                "",
                "Within the \"dependencies\" field, each dependency can have a minimum constraint listed. These "
                "minimum constraints will be used when transitively depending upon this library. A minimum "
                "port-version can additionally be specified with a '#' suffix.")
DECLARE_MESSAGE(HelpVersioning,
                (),
                "",
                "Versioning allows you to deterministically control the precise revisions of dependencies used by "
                "your project from within your manifest file.")
DECLARE_MESSAGE(HelpVersionScheme, (), "", "A dot-separated sequence of numbers (1.2.3.4)")
DECLARE_MESSAGE(HelpVersionSchemes, (), "", "The following versioning schemes are accepted.")
DECLARE_MESSAGE(HelpVersionSemverScheme, (), "", "A Semantic Version 2.0 (2.1.0-rc2)")
DECLARE_MESSAGE(HelpVersionStringScheme, (), "", "An exact, incomparable version (Vista)")
DECLARE_MESSAGE(
    IgnoringVcpkgRootEnvironment,
    (msg::path, msg::actual, msg::value),
    "{actual} is the path we actually used, {value} is the path to vcpkg's binary",
    "The vcpkg {value} is using detected vcpkg root {actual} and ignoring mismatched VCPKG_ROOT environment "
    "value {path}. To suppress this message, unset the environment variable or use the --vcpkg-root command line "
    "switch.")
DECLARE_MESSAGE(IllegalFeatures, (), "", "List of features is not allowed in this context")
DECLARE_MESSAGE(IllegalPlatformSpec, (), "", "Platform qualifier is not allowed in this context")
DECLARE_MESSAGE(ImproperShaLength, (msg::value), "{value} is a sha.", "SHA512's must be 128 hex characters: {value}")
DECLARE_MESSAGE(IncorrectArchiveFileSignature, (), "", "Incorrect archive file signature")
DECLARE_MESSAGE(InfoSetEnvVar,
                (msg::env_var),
                "In this context 'editor' means IDE",
                "You can also set {env_var} to your editor of choice.")
DECLARE_MESSAGE(InitRegistryFailedNoRepo,
                (msg::path, msg::command_line),
                "",
                "Could not create a registry at {path} because this is not a git repository root.\nUse `git init "
                "{command_line}` to create a git repository in this folder.")
DECLARE_MESSAGE(InstallCopiedFile,
                (msg::path_source, msg::path_destination),
                "",
                "{path_source} -> {path_destination} done")
DECLARE_MESSAGE(InstalledBy, (msg::path), "", "Installed by {path}")
DECLARE_MESSAGE(InstalledPackages, (), "", "The following packages are already installed:")
DECLARE_MESSAGE(InstalledRequestedPackages, (), "", "All requested packages are currently installed.")
DECLARE_MESSAGE(InstallFailed, (msg::path, msg::error_msg), "", "failed: {path}: {error_msg}")
DECLARE_MESSAGE(InstallingMavenFile,
                (msg::path),
                "Printed after a filesystem operation error",
                "{path} installing Maven file")
DECLARE_MESSAGE(InstallingOverlayPort, (), "", "installing overlay port from here")
DECLARE_MESSAGE(InstallingPackage,
                (msg::action_index, msg::count, msg::spec),
                "",
                "Installing {action_index}/{count} {spec}...")
DECLARE_MESSAGE(InstallPackageInstruction,
                (msg::value, msg::path),
                "'{value}' is the nuget id.",
                "With a project open, go to Tools->NuGet Package Manager->Package Manager Console and "
                "paste:\n Install-Package \"{value}\" -Source \"{path}\"")
DECLARE_MESSAGE(InstallRootDir, (), "", "Installed directory (experimental)")
DECLARE_MESSAGE(InstallSkippedUpToDateFile,
                (msg::path_source, msg::path_destination),
                "",
                "{path_source} -> {path_destination} skipped, up to date")
DECLARE_MESSAGE(InstallWithSystemManager,
                (),
                "",
                "You may be able to install this tool via your system package manager.")
DECLARE_MESSAGE(InstallWithSystemManagerMono,
                (msg::url),
                "",
                "Ubuntu 18.04 users may need a newer version of mono, available at {url}.")
DECLARE_MESSAGE(InstallWithSystemManagerPkg,
                (msg::command_line),
                "",
                "You may be able to install this tool via your system package manager ({command_line}).")
DECLARE_MESSAGE(IntegrateBashHelp,
                (),
                "'bash' is a terminal program which should be unlocalized.",
                "Enable bash tab-completion. Non-Windows only")
DECLARE_MESSAGE(IntegrateFishHelp,
                (),
                "'fish' is a terminal program which should be unlocalized.",
                "Enable fish tab-completion. Non-Windows only")
DECLARE_MESSAGE(IntegrateInstallHelpLinux, (), "", "Makes installed packages available user-wide")
DECLARE_MESSAGE(IntegrateInstallHelpWindows,
                (),
                "",
                "Makes installed packages available user-wide. Requires admin privileges on first use")
DECLARE_MESSAGE(IntegrateNonWindowsOnly,
                (msg::command_line),
                "",
                "{command_line} is non-Windows-only and not supported on this system.")
DECLARE_MESSAGE(IntegratePowerShellHelp, (), "", "Enable PowerShell tab-completion. Windows-only")
DECLARE_MESSAGE(IntegrateProjectHelp,
                (),
                "",
                "Generates a referencing NuGet package for individual Visual Studio project use. Windows-only")
DECLARE_MESSAGE(IntegrateRemoveHelp, (), "", "Removes user-wide integration")
DECLARE_MESSAGE(IntegrateWindowsOnly,
                (msg::command_line),
                "",
                "{command_line} is Windows-only and not supported on this system.")
DECLARE_MESSAGE(IntegrateZshHelp,
                (),
                "'zsh' is a terminal program which should be unlocalized.",
                "Enable zsh tab-completion. Non-Windows only")
DECLARE_MESSAGE(IntegrationFailedVS2015, (), "", "Integration was not applied for Visual Studio 2015.")
DECLARE_MESSAGE(InternalCICommand,
                (),
                "",
                "vcpkg ci is an internal command which will change incompatibly or be removed at any time.")
DECLARE_MESSAGE(
    InternalErrorMessageContact,
    (),
    "",
    "Please open an issue at "
    "https://github.com/microsoft/vcpkg/issues/new?template=other-type-of-bug-report.md&labels=category:vcpkg-bug "
    "with detailed steps to reproduce the problem.")
DECLARE_MESSAGE(InvalidArchitecture,
                (msg::value),
                "{value} is what the user entered that we did not understand",
                "invalid architecture: {value}")
DECLARE_MESSAGE(InvalidArgument, (), "", "invalid argument")
DECLARE_MESSAGE(
    InvalidArgumentRequiresAbsolutePath,
    (msg::binary_source),
    "",
    "invalid argument: binary config '{binary_source}' path arguments for binary config strings must be absolute")
DECLARE_MESSAGE(
    InvalidArgumentRequiresBaseUrl,
    (msg::base_url, msg::binary_source),
    "",
    "invalid argument: binary config '{binary_source}' requires a {base_url} base url as the first argument")
DECLARE_MESSAGE(InvalidArgumentRequiresBaseUrlAndToken,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires at least a base-url and a SAS token")
DECLARE_MESSAGE(InvalidArgumentRequiresNoneArguments,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' does not take arguments")
DECLARE_MESSAGE(InvalidArgumentRequiresNoWildcards,
                (msg::path),
                "",
                "cannot fix Windows path case for path containing wildcards: {path}")
DECLARE_MESSAGE(InvalidArgumentRequiresOneOrTwoArguments,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires 1 or 2 arguments")
DECLARE_MESSAGE(InvalidArgumentRequiresPathArgument,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires at least one path argument")
DECLARE_MESSAGE(InvalidArgumentRequiresPrefix,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires at least one prefix")
DECLARE_MESSAGE(InvalidArgumentRequiresSingleArgument,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' does not take more than 1 argument")
DECLARE_MESSAGE(InvalidArgumentRequiresSingleStringArgument,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' expects a single string argument")
DECLARE_MESSAGE(InvalidArgumentRequiresSourceArgument,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires at least one source argument")
DECLARE_MESSAGE(InvalidArgumentRequiresTwoOrThreeArguments,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires 2 or 3 arguments")
DECLARE_MESSAGE(InvalidArgumentRequiresValidToken,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires a SAS token without a "
                "preceeding '?' as the second argument")
DECLARE_MESSAGE(InvalidArgumentRequiresZeroOrOneArgument,
                (msg::binary_source),
                "",
                "invalid argument: binary config '{binary_source}' requires 0 or 1 argument")
DECLARE_MESSAGE(InvalidBuildInfo, (msg::error_msg), "", "Invalid BUILD_INFO file for package: {error_msg}")
DECLARE_MESSAGE(
    InvalidBuiltInBaseline,
    (msg::value),
    "{value} is a git commit sha",
    "the top-level builtin-baseline ({value}) was not a valid commit sha: expected 40 hexadecimal characters.")
DECLARE_MESSAGE(InvalidBundleDefinition, (), "", "Invalid bundle definition.")
DECLARE_MESSAGE(InvalidCharacterInFeatureList,
                (),
                "",
                "invalid character in feature name (must be lowercase, digits, '-', or '*')")
DECLARE_MESSAGE(InvalidCharacterInFeatureName,
                (),
                "",
                "invalid character in feature name (must be lowercase, digits, '-')")
DECLARE_MESSAGE(InvalidCharacterInPortName, (), "", "invalid character in port name (must be lowercase, digits, '-')")
DECLARE_MESSAGE(InvalidCodePoint, (), "", "Invalid code point passed to utf8_encoded_code_point_count")
DECLARE_MESSAGE(InvalidCodeUnit, (), "", "invalid code unit")
DECLARE_MESSAGE(InvalidCommandArgSort,
                (),
                "",
                "Value of --sort must be one of 'lexicographical', 'topological', 'reverse'.")
DECLARE_MESSAGE(InvalidCommentStyle,
                (),
                "",
                "vcpkg does not support c-style comments, however most objects allow $-prefixed fields to be used as "
                "comments.")
DECLARE_MESSAGE(InvalidCommitId, (msg::commit_sha), "", "Invalid commit id: {commit_sha}")
DECLARE_MESSAGE(InvalidDefaultFeatureName, (), "", "'default' is a reserved feature name")
DECLARE_MESSAGE(InvalidFeature,
                (),
                "",
                "features must be lowercase alphanumeric+hyphens, and not one of the reserved names")
DECLARE_MESSAGE(InvalidFilename,
                (msg::value, msg::path),
                "'{value}' is a list of invalid characters. I.e. \\/:*?<>|",
                "Filename cannot contain invalid chars {value}, but was {path}")
DECLARE_MESSAGE(InvalidFileType, (msg::path), "", "failed: {path} cannot handle file type")
DECLARE_MESSAGE(InvalidFloatingPointConst, (msg::count), "", "Invalid floating point constant: {count}")
DECLARE_MESSAGE(InvalidFormatString,
                (msg::actual),
                "{actual} is the provided format string",
                "invalid format string: {actual}")
DECLARE_MESSAGE(InvalidHexDigit, (), "", "Invalid hex digit in unicode escape")
DECLARE_MESSAGE(InvalidIntegerConst, (msg::count), "", "Invalid integer constant: {count}")
DECLARE_MESSAGE(InvalidLibraryMissingLinkerMembers, (), "", "Library was invalid: could not find a linker member.")
DECLARE_MESSAGE(
    InvalidLinkage,
    (msg::system_name, msg::value),
    "'{value}' is the linkage type vcpkg would did not understand. (Correct values would be static ofr dynamic)",
    "Invalid {system_name} linkage type: [{value}]")
DECLARE_MESSAGE(InvalidLogicExpressionUnexpectedCharacter, (), "", "invalid logic expression, unexpected character")
DECLARE_MESSAGE(InvalidLogicExpressionUsePipe, (), "", "invalid logic expression, use '|' rather than 'or'")
DECLARE_MESSAGE(InvalidOptionForRemove,
                (),
                "'remove' is a command that should not be changed.",
                "'remove' accepts either libraries or '--outdated'")
DECLARE_MESSAGE(InvalidPortVersonName, (msg::path), "", "Found invalid port version file name: `{path}`.")
DECLARE_MESSAGE(InvalidSharpInVersion, (), "", "invalid character '#' in version text")
DECLARE_MESSAGE(InvalidSharpInVersionDidYouMean,
                (msg::value),
                "{value} is an integer. `\"port-version\":' is JSON syntax and should be unlocalized",
                "invalid character '#' in version text. Did you mean \"port-version\": {value}?")
DECLARE_MESSAGE(InvalidString, (), "", "Invalid utf8 passed to Value::string(std::string)")
DECLARE_MESSAGE(InvalidTriplet, (msg::triplet), "", "Invalid triplet: {triplet}")
DECLARE_MESSAGE(InvalidValueHashAdditionalFiles,
                (msg::path),
                "",
                "Variable VCPKG_HASH_ADDITIONAL_FILES contains invalid file path: '{path}'. The value must be "
                "an absolute path to an existent file.")
DECLARE_MESSAGE(IrregularFile, (msg::path), "", "path was not a regular file: {path}")
DECLARE_MESSAGE(JsonErrorMustBeAnObject, (msg::path), "", "Expected \"{path}\" to be an object.")
DECLARE_MESSAGE(JsonFieldNotObject, (msg::json_field), "", "value of [\"{json_field}\"] must be an object")
DECLARE_MESSAGE(JsonFieldNotString, (msg::json_field), "", "value of [\"{json_field}\"] must be a string")
DECLARE_MESSAGE(JsonFileMissingExtension,
                (msg::path),
                "",
                "the JSON file {path} must have a .json (all lowercase) extension")
DECLARE_MESSAGE(JsonSwitch, (), "", "Prints JSON rather than plain text")
DECLARE_MESSAGE(JsonValueNotArray, (), "", "json value is not an array")
DECLARE_MESSAGE(JsonValueNotObject, (), "", "json value is not an object")
DECLARE_MESSAGE(JsonValueNotString, (), "", "json value is not a string")
DECLARE_MESSAGE(LaunchingProgramFailed,
                (msg::tool_name),
                "A platform API call failure message is appended after this",
                "Launching {tool_name}:")
DECLARE_MESSAGE(LibraryArchiveMemberTooSmall,
                (),
                "",
                "A library archive member was too small to contain the expected data type.")
DECLARE_MESSAGE(LibraryFirstLinkerMemberMissing, (), "", "Could not find first linker member name.")
DECLARE_MESSAGE(LicenseExpressionString, (), "", "<license string>")
DECLARE_MESSAGE(LicenseExpressionContainsExtraPlus,
                (),
                "",
                "SPDX license expression contains an extra '+'. These are only allowed directly "
                "after a license identifier.")
DECLARE_MESSAGE(LicenseExpressionContainsInvalidCharacter,
                (msg::value),
                "example of {value:02X} is '7B'\nexample of {value} is '{'",
                "SPDX license expression contains an invalid character (0x{value:02X} '{value}').")
DECLARE_MESSAGE(LicenseExpressionContainsUnicode,
                (msg::value, msg::pretty_value),
                "example of {value:04X} is '22BB'\nexample of {pretty_value} is '⊻'",
                "SPDX license expression contains a unicode character (U+{value:04X} '{pretty_value}'), but these "
                "expressions are ASCII-only.")
DECLARE_MESSAGE(LicenseExpressionDocumentRefUnsupported,
                (),
                "",
                "The current implementation does not support DocumentRef- SPDX references.")
DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundParen,
                (),
                "",
                "Expected a compound or the end of the string, found a parenthesis.")
DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundWith,
                (),
                "AND, OR, and WITH are all keywords and should not be translated.",
                "Expected either AND or OR, found WITH (WITH is only allowed after license names, not "
                "parenthesized expressions).")
DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundWord,
                (msg::value),
                "Example of {value} is 'MIT'.\nAND and OR are both keywords and should not be translated.",
                "Expected either AND or OR, found a license or exception name: '{value}'.")
DECLARE_MESSAGE(LicenseExpressionExpectCompoundOrWithFoundWord,
                (msg::value),
                "example of {value} is 'MIT'.\nAND, OR, and WITH are all keywords and should not be translated.",
                "Expected either AND, OR, or WITH, found a license or exception name: '{value}'.")
DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundCompound,
                (msg::value),
                "Example of {value} is 'AND'",
                "Expected an exception name, found the compound {value}.")
DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundEof,
                (),
                "",
                "Expected an exception name, found the end of the string.")
DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundParen, (), "", "Expected an exception name, found a parenthesis.")
DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundCompound,
                (msg::value),
                "Example of {value} is 'AND'",
                "Expected a license name, found the compound {value}.")
DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundEof, (), "", "Expected a license name, found the end of the string.")
DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundParen, (), "", "Expected a license name, found a parenthesis.")
DECLARE_MESSAGE(LicenseExpressionImbalancedParens,
                (),
                "",
                "There was a close parenthesis without an opening parenthesis.")
DECLARE_MESSAGE(LicenseExpressionUnknownException,
                (msg::value),
                "Example of {value} is 'unknownexception'",
                "Unknown license exception identifier '{value}'. Known values are listed at "
                "https://spdx.org/licenses/exceptions-index.html")
DECLARE_MESSAGE(LicenseExpressionUnknownLicense,
                (msg::value),
                "Example of {value} is 'unknownlicense'",
                "Unknown license identifier '{value}'. Known values are listed at https://spdx.org/licenses/")
DECLARE_MESSAGE(LinkageDynamicDebug, (), "", "Dynamic Debug (/MDd)")
DECLARE_MESSAGE(LinkageDynamicRelease, (), "", "Dynamic Release (/MD)")
DECLARE_MESSAGE(LinkageStaticDebug, (), "", "Static Debug (/MTd)")
DECLARE_MESSAGE(LinkageStaticRelease, (), "", "Static Release (/MT)")
DECLARE_MESSAGE(ListHelp, (), "", "Lists installed libraries")
DECLARE_MESSAGE(LoadedCommunityTriplet,
                (),
                "",
                "loaded community triplet from here. Community triplets are not built in the curated registry and are "
                "thus less likely to succeed.")
DECLARE_MESSAGE(LoadedOverlayTriplet, (), "", "loaded overlay triplet from here")
DECLARE_MESSAGE(LoadingDependencyInformation,
                (msg::count),
                "",
                "Loading dependency information for {count} packages...")
DECLARE_MESSAGE(LocalPortfileVersion,
                (),
                "",
                "Using local portfile versions. To update the local portfiles, use `git pull`.")
DECLARE_MESSAGE(ManifestConflict2, (), "", "Found both a manifest and CONTROL files; please rename one or the other")
DECLARE_MESSAGE(ManifestFormatCompleted, (), "", "Succeeded in formatting the manifest files.")
DECLARE_MESSAGE(MismatchedBinParagraphs,
                (),
                "",
                "The serialized binary paragraph was different from the original binary paragraph. Please open an "
                "issue at https://github.com/microsoft/vcpkg with the following output:")
DECLARE_MESSAGE(MismatchedFiles, (), "", "file to store does not match hash")
DECLARE_MESSAGE(MismatchedManifestAfterReserialize,
                (),
                "The original file output and generated output are printed after this line, in English as it's "
                "intended to be used in the issue submission and read by devs. This message indicates an internal "
                "error in vcpkg.",
                "The serialized manifest was different from the original manifest. Please open an issue at "
                "https://github.com/microsoft/vcpkg, with the following output:")
DECLARE_MESSAGE(MismatchedNames,
                (msg::package_name, msg::actual),
                "{actual} is the port name found",
                "names did not match: '{package_name}' != '{actual}'")
DECLARE_MESSAGE(MismatchedSpec,
                (msg::path, msg::expected, msg::actual),
                "{expected} and {actual} are package specs like 'zlib:x64-windows'",
                "Mismatched spec in port {path}: expected {expected}, actual {actual}")
DECLARE_MESSAGE(MismatchedType,
                (msg::json_field, msg::json_type),
                "",
                "{json_field}: mismatched type: expected {json_type}")
DECLARE_MESSAGE(Missing7zHeader, (), "", "Unable to find 7z header.")
DECLARE_MESSAGE(MissingAndroidEnv, (), "", "ANDROID_NDK_HOME environment variable missing")
DECLARE_MESSAGE(MissingAndroidHomeDir, (msg::path), "", "ANDROID_NDK_HOME directory does not exist: {path}")
DECLARE_MESSAGE(MissingArgFormatManifest,
                (),
                "",
                "format-manifest was passed --convert-control without '--all'.\nThis doesn't do anything: control "
                "files passed explicitly are converted automatically.")
DECLARE_MESSAGE(MissingClosingParen, (), "", "missing closing )")
DECLARE_MESSAGE(MissingDependency,
                (msg::spec, msg::package_name),
                "",
                "Package {spec} is installed, but dependency {package_name} is not.")
DECLARE_MESSAGE(MissingExtension, (msg::extension), "", "Missing '{extension}' extension.")
DECLARE_MESSAGE(MissingOption, (msg::option), "", "This command requires --{option}")
DECLARE_MESSAGE(MissingOrInvalidIdentifer, (), "", "missing or invalid identifier")
DECLARE_MESSAGE(MissingPortSuggestPullRequest,
                (),
                "",
                "If your port is not listed, please open an issue at and/or consider making a pull request.")
DECLARE_MESSAGE(MissingRequiredField,
                (msg::json_field, msg::json_type),
                "Example completely formatted message:\nerror: missing required field 'dependencies' (an array of "
                "dependencies)",
                "missing required field '{json_field}' ({json_type})")
DECLARE_MESSAGE(MissingRequiredField2, (msg::json_field), "", "missing required field '{json_field}'")
DECLARE_MESSAGE(MixingBooleanOperationsNotAllowed,
                (),
                "",
                "mixing & and | is not allowed; use () to specify order of operations")
DECLARE_MESSAGE(MonoInstructions,
                (),
                "",
                "This may be caused by an incomplete mono installation. Full mono is "
                "available on some systems via `sudo apt install mono-complete`. Ubuntu 18.04 users may "
                "need a newer version of mono, available at https://www.mono-project.com/download/stable/")
DECLARE_MESSAGE(MultiArch, (msg::option), "", "Multi-Arch must be 'same' but was {option}")
DECLARE_MESSAGE(MultipleFeatures,
                (msg::package_name, msg::feature),
                "",
                "{package_name} declares {feature} multiple times; please ensure that features have distinct names")
DECLARE_MESSAGE(MutuallyExclusiveOption,
                (msg::value, msg::option),
                "{value} is a second {option} switch",
                "--{value} cannot be used with --{option}.")
DECLARE_MESSAGE(NewConfigurationAlreadyExists,
                (msg::path),
                "",
                "Creating a manifest would overwrite a vcpkg-configuration.json at {path}.")
DECLARE_MESSAGE(NewManifestAlreadyExists, (msg::path), "", "A manifest is already present at {path}.")
DECLARE_MESSAGE(NewNameCannotBeEmpty, (), "", "--name cannot be empty.")
DECLARE_MESSAGE(NewOnlyOneVersionKind,
                (),
                "",
                "Only one of --version-relaxed, --version-date, or --version-string may be specified.")
DECLARE_MESSAGE(NewSpecifyNameVersionOrApplication,
                (),
                "",
                "Either specify --name and --version to produce a manifest intended for C++ libraries, or specify "
                "--application to indicate that the manifest is not intended to be used as a port.")
DECLARE_MESSAGE(NewVersionCannotBeEmpty, (), "", "--version cannot be empty.")
DECLARE_MESSAGE(NoError, (), "", "no error")
DECLARE_MESSAGE(NoInstalledPackages,
                (),
                "The name 'search' is the name of a command that is not localized.",
                "No packages are installed. Did you mean `search`?")
DECLARE_MESSAGE(NonExactlyArgs,
                (msg::command_name, msg::expected, msg::actual),
                "{expected} and {actual} are integers",
                "the command '{command_name}' requires exactly {expected} arguments, but {actual} were provided")
DECLARE_MESSAGE(NonOneRemainingArgs,
                (msg::command_name),
                "",
                "the command '{command_name}' requires exactly one argument")
DECLARE_MESSAGE(NonRangeArgs,
                (msg::command_name, msg::lower, msg::upper, msg::actual),
                "{actual} is an integer",
                "the command '{command_name}' requires between {lower} and {upper} arguments, inclusive, but {actual} "
                "were provided")
DECLARE_MESSAGE(NonZeroOrOneRemainingArgs,
                (msg::command_name),
                "",
                "the command '{command_name}' requires zero or one arguments")
DECLARE_MESSAGE(NonZeroRemainingArgs,
                (msg::command_name),
                "",
                "the command '{command_name}' does not accept any additional arguments")
DECLARE_MESSAGE(NoOutdatedPackages, (), "", "There are no outdated packages.")
DECLARE_MESSAGE(NoRegistryForPort, (msg::package_name), "", "no registry configured for port {package_name}")
DECLARE_MESSAGE(NoUrlsAndHashSpecified, (msg::sha), "", "No urls specified to download SHA: {sha}")
DECLARE_MESSAGE(NoUrlsAndNoHashSpecified, (), "", "No urls specified and no hash specified.")
DECLARE_MESSAGE(NugetOutputNotCapturedBecauseInteractiveSpecified,
                (),
                "",
                "NuGet command failed and output was not captured because --interactive was specified")
DECLARE_MESSAGE(NugetPackageFileSucceededButCreationFailed,
                (msg::path),
                "",
                "NuGet package creation succeeded, but no .nupkg was produced. Expected: \"{path}\"")
DECLARE_MESSAGE(NugetTimeoutExpectsSinglePositiveInteger,
                (),
                "",
                "unexpected arguments: binary config 'nugettimeout' expects a single positive integer argument")
DECLARE_MESSAGE(OptionMustBeInteger, (msg::option), "", "Value of --{option} must be an integer.")
DECLARE_MESSAGE(OptionRequired, (msg::option), "", "--{option} option is required.")
DECLARE_MESSAGE(OptionRequiresAValue, (msg::option), "", "the option '{option}' requires a value")
DECLARE_MESSAGE(OptionRequiresANonDashesValue,
                (msg::option, msg::actual, msg::value),
                "{value} is the value the user typed, {actual} is {option} potentially with prefixes like '--x-'. Full "
                "example: the option 'evil-option' requires a value; if you intended to set 'evil-option' to "
                "'--evil-value', use the equals form instead: --x-evil-option=--evil-value",
                "the option '{option}' requires a value; if you intended to set '{option}' to '{value}', use the "
                "equals form instead: {actual}={value}")
DECLARE_MESSAGE(OptionUsedMultipleTimes, (msg::option), "", "the option '{option}' was specified multiple times")
DECLARE_MESSAGE(OptionRequiresOption,
                (msg::value, msg::option),
                "{value} is a command line option.",
                "--{value} requires --{option}")
DECLARE_MESSAGE(Options, (), "Printed just before a list of options for a command", "Options")
DECLARE_MESSAGE(OriginalBinParagraphHeader, (), "", "\nOriginal Binary Paragraph")
DECLARE_MESSAGE(OtherCommandsHeader, (), "", "Other")
DECLARE_MESSAGE(OverlayPatchDir, (msg::path), "", "Overlay path \"{path}\" must exist and must be a directory.")
DECLARE_MESSAGE(OverlayPortsDirectoriesHelp, (msg::env_var), "", "Directories of overlay ports (also: {env_var})")
DECLARE_MESSAGE(OverlayTripletDirectoriesHelp, (msg::env_var), "", "Directories of overlay triplets (also: {env_var})")
DECLARE_MESSAGE(OverlayTriplets, (msg::path), "", "Overlay Triplets from \"{path}\":")
DECLARE_MESSAGE(OverwritingFile, (msg::path), "", "File {path} was already present and will be overwritten")
DECLARE_MESSAGE(PackageAbi, (msg::spec, msg::package_abi), "", "{spec} package ABI: {package_abi}")
DECLARE_MESSAGE(PackageAlreadyRemoved, (msg::spec), "", "unable to remove {spec}: already removed")
DECLARE_MESSAGE(PackageDiscoveryHeader, (), "", "Package Discovery")
DECLARE_MESSAGE(PackageManipulationHeader, (), "", "Package Manipulation")
DECLARE_MESSAGE(PackageInfoHelp, (), "", "Display detailed information on packages")
DECLARE_MESSAGE(PackageFailedtWhileExtracting,
                (msg::value, msg::path),
                "'{value}' is either a tool name or a package name.",
                "'{value}' failed while extracting {path}.")
DECLARE_MESSAGE(PackageInstallationHeader, (), "", "Package Installation")
DECLARE_MESSAGE(PackageRootDir, (), "", "Packages directory (experimental)")
DECLARE_MESSAGE(PackagesToInstall, (), "", "The following packages will be built and installed:")
DECLARE_MESSAGE(PackagesToModify, (), "", "Additional packages (*) will be modified to complete this operation.")
DECLARE_MESSAGE(PackagesToRebuild, (), "", "The following packages will be rebuilt:")
DECLARE_MESSAGE(PackagesToRebuildSuggestRecurse,
                (),
                "",
                "If you are sure you want to rebuild the above packages, run the command with the --recurse option.")
DECLARE_MESSAGE(PackagesToRemove, (), "", "The following packages will be removed:")
DECLARE_MESSAGE(PackagesUpToDate, (), "", "No packages need updating.")
DECLARE_MESSAGE(PackingVendorFailed, (msg::vendor), "", "Packing {vendor} failed. Use --debug for more information.")
DECLARE_MESSAGE(PairedSurrogatesAreInvalid,
                (),
                "",
                "trailing surrogate following leading surrogate (paired surrogates are invalid)")
DECLARE_MESSAGE(ParagraphDuplicateField, (), "", "duplicate field")
DECLARE_MESSAGE(ParagraphExactlyOne, (), "", "There should be exactly one paragraph")
DECLARE_MESSAGE(ParagraphExpectedColonAfterField, (), "", "expected ':' after field name")
DECLARE_MESSAGE(ParagraphExpectedFieldName, (), "", "expected field name")
DECLARE_MESSAGE(ParagraphUnexpectedEndOfLine, (), "", "unexpected end of line, to span a blank line use \"  .\"")
DECLARE_MESSAGE(
    ParseFeatureNameError,
    (msg::package_name, msg::url),
    "",
    "\"{package_name}\" is not a valid feature name. "
    "Feature names must be lowercase alphanumeric+hypens and not reserved (see {url} for more information).")
DECLARE_MESSAGE(ParseIdentifierError,
                (msg::value, msg::url),
                "{value} is a lowercase identifier like 'boost'",
                "\"{value}\" is not a valid identifier. "
                "Identifiers must be lowercase alphanumeric+hypens and not reserved (see {url} for more information).")
DECLARE_MESSAGE(
    ParsePackageNameNotEof,
    (msg::url),
    "",
    "expected the end of input parsing a package name; this usually means the indicated character is not allowed to be "
    "in a port name. Port names are all lowercase alphanumeric+hypens and not reserved (see {url} for more "
    "information).")
DECLARE_MESSAGE(
    ParsePackageNameError,
    (msg::package_name, msg::url),
    "",
    "\"{package_name}\" is not a valid package name. "
    "Package names must be lowercase alphanumeric+hypens and not reserved (see {url} for more information).")
DECLARE_MESSAGE(ParsePackagePatternError,
                (msg::package_name, msg::url),
                "",
                "\"{package_name}\" is not a valid package pattern. "
                "Package patterns must use only one wildcard character (*) and it must be the last character in "
                "the pattern (see {url} for more information).")
DECLARE_MESSAGE(
    ParseQualifiedSpecifierNotEof,
    (),
    "",
    "expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be "
    "in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hypens.")
DECLARE_MESSAGE(ParseQualifiedSpecifierNotEofSquareBracket,
                (msg::version_spec),
                "",
                "expected the end of input parsing a package spec; did you mean {version_spec} instead?")
DECLARE_MESSAGE(PathMustBeAbsolute,
                (msg::path),
                "",
                "Value of environment variable X_VCPKG_REGISTRIES_CACHE is not absolute: {path}")
DECLARE_MESSAGE(PECoffHeaderTooShort,
                (msg::path),
                "Portable executable is a term-of-art, see https://learn.microsoft.com/windows/win32/debug/pe-format",
                "While parsing Portable Executable {path}, size of COFF header too small to contain a valid PE header.")
DECLARE_MESSAGE(PEConfigCrossesSectionBoundary,
                (msg::path),
                "Portable executable is a term-of-art, see https://learn.microsoft.com/windows/win32/debug/pe-format",
                "While parsing Portable Executable {path}, image config directory crosses a section boundary.")
DECLARE_MESSAGE(PEImportCrossesSectionBoundary,
                (msg::path),
                "Portable executable is a term-of-art, see https://learn.microsoft.com/windows/win32/debug/pe-format",
                "While parsing Portable Executable {path}, import table crosses a section boundary.")
DECLARE_MESSAGE(PEPlusTagInvalid,
                (msg::path),
                "Portable executable is a term-of-art, see https://learn.microsoft.com/windows/win32/debug/pe-format",
                "While parsing Portable Executable {path}, optional header was neither PE32 nor PE32+.")
DECLARE_MESSAGE(PERvaNotFound,
                (msg::path, msg::value),
                "{value:#X} is the Relative Virtual Address sought. Portable executable is a term-of-art, see "
                "https://learn.microsoft.com/windows/win32/debug/pe-format",
                "While parsing Portable Executable {path}, could not find RVA {value:#X}.")
DECLARE_MESSAGE(PerformingPostBuildValidation, (), "", "-- Performing post-build validation")
DECLARE_MESSAGE(PortBugBinDirExists,
                (msg::path),
                "",
                "There should be no bin\\ directory in a static build, but {path} is present.")
DECLARE_MESSAGE(PortBugDebugBinDirExists,
                (msg::path),
                "",
                "There should be no debug\\bin\\ directory in a static build, but {path} is present.")
DECLARE_MESSAGE(PortBugDebugShareDir,
                (),
                "",
                "/debug/share should not exist. Please reorganize any important files, then use\n"
                "file(REMOVE_RECURSE \"${{CURRENT_PACKAGES_DIR}}/debug/share\")")
DECLARE_MESSAGE(PortBugDllAppContainerBitNotSet,
                (),
                "",
                "The App Container bit must be set for Windows Store apps. The following DLLs do not have the App "
                "Container bit set:")
DECLARE_MESSAGE(PortBugDllInLibDir,
                (),
                "",
                "The following dlls were found in /lib or /debug/lib. Please move them to /bin or "
                "/debug/bin, respectively.")
DECLARE_MESSAGE(PortBugDuplicateIncludeFiles,
                (),
                "",
                "Include files should not be duplicated into the /debug/include directory. If this cannot "
                "be disabled in the project cmake, use\n"
                "file(REMOVE_RECURSE \"${{CURRENT_PACKAGES_DIR}}/debug/include\")")
DECLARE_MESSAGE(PortBugFoundCopyrightFiles, (), "", "The following files are potential copyright files:")
DECLARE_MESSAGE(PortBugFoundDebugBinaries, (msg::count), "", "Found {count} debug binaries:")
DECLARE_MESSAGE(PortBugFoundDllInStaticBuild,
                (),
                "",
                "DLLs should not be present in a static build, but the following DLLs were found:")
DECLARE_MESSAGE(PortBugFoundEmptyDirectories,
                (msg::path),
                "",
                "There should be no empty directories in {path}. The following empty directories were found:")
DECLARE_MESSAGE(PortBugFoundExeInBinDir,
                (),
                "",
                "The following EXEs were found in /bin or /debug/bin. EXEs are not valid distribution targets.")
DECLARE_MESSAGE(PortBugFoundReleaseBinaries, (msg::count), "", "Found {count} release binaries:")
DECLARE_MESSAGE(PortBugIncludeDirInCMakeHelperPort,
                (),
                "",
                "The folder /include exists in a cmake helper port; this is incorrect, since only cmake "
                "files should be installed")
DECLARE_MESSAGE(PortBugInspectFiles, (msg::extension), "", "To inspect the {extension} files, use:")
DECLARE_MESSAGE(
    PortBugInvalidCrtLinkage,
    (msg::expected),
    "{expected} is one of LinkageDynamicDebug/LinkageDynamicRelease/LinkageStaticDebug/LinkageStaticRelease. "
    "Immediately after this message is a file by file list with what linkages they contain. 'CRT' is an acronym "
    "meaning C Runtime. See also: "
    "https://learn.microsoft.com/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170. This is "
    "complicated because a binary can link with more than one CRT.\n"
    "Example fully formatted message:\n"
    "The following binaries should use the Dynamic Debug (/MDd) CRT.\n"
    "    C:\\some\\path\\to\\sane\\lib links with: Dynamic Release (/MD)\n"
    "    C:\\some\\path\\to\\lib links with:\n"
    "        Static Debug (/MTd)\n"
    "        Dynamic Release (/MD)\n"
    "    C:\\some\\different\\path\\to\\a\\dll links with:\n"
    "        Static Debug (/MTd)\n"
    "        Dynamic Debug (/MDd)\n",
    "The following binaries should use the {expected} CRT.")
DECLARE_MESSAGE(PortBugInvalidCrtLinkageEntry,
                (msg::path),
                "See explanation in PortBugInvalidCrtLinkage",
                "{path} links with:")
DECLARE_MESSAGE(PortBugKernel32FromXbox,
                (),
                "",
                "The selected triplet targets Xbox, but the following DLLs link with kernel32. These DLLs cannot be "
                "loaded on Xbox, where kernel32 is not present. This is typically caused by linking with kernel32.lib "
                "rather than a suitable umbrella library, such as onecore_apiset.lib or xgameplatform.lib.")
DECLARE_MESSAGE(
    PortBugMergeLibCMakeDir,
    (msg::package_name),
    "",
    "The /lib/cmake folder should be merged with /debug/lib/cmake and moved to /share/{package_name}/cmake. "
    "Please use the helper function `vcpkg_cmake_config_fixup()` from the port vcpkg-cmake-config.`")
DECLARE_MESSAGE(PortBugMismatchedNumberOfBinaries, (), "", "Mismatching number of debug and release binaries.")
DECLARE_MESSAGE(
    PortBugMisplacedCMakeFiles,
    (msg::spec),
    "",
    "The following cmake files were found outside /share/{spec}. Please place cmake files in /share/{spec}.")
DECLARE_MESSAGE(PortBugMisplacedFiles, (msg::path), "", "The following files are placed in {path}:")
DECLARE_MESSAGE(PortBugMisplacedFilesCont, (), "", "Files cannot be present in those directories.")
DECLARE_MESSAGE(PortBugMisplacedPkgConfigFiles,
                (),
                "",
                "pkgconfig directories should be one of share/pkgconfig (for header only libraries only), "
                "lib/pkgconfig, or lib/debug/pkgconfig. The following misplaced pkgconfig files were found:")
DECLARE_MESSAGE(PortBugMissingDebugBinaries, (), "", "Debug binaries were not found.")
DECLARE_MESSAGE(PortBugMissingFile,
                (msg::path),
                "",
                "The /{path} file does not exist. This file must exist for CMake helper ports.")
DECLARE_MESSAGE(
    PortBugMissingProvidedUsage,
    (msg::package_name),
    "",
    "The port provided \"usage\" but forgot to install to /share/{package_name}/usage, add the following line"
    "in the portfile:")
DECLARE_MESSAGE(PortBugMissingImportedLibs,
                (msg::path),
                "",
                "Import libraries were not present in {path}.\nIf this is intended, add the following line in the "
                "portfile:\nset(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)")
DECLARE_MESSAGE(PortBugMissingIncludeDir,
                (),
                "",
                "The folder /include is empty or not present. This indicates the library was not correctly "
                "installed.")
DECLARE_MESSAGE(PortBugMissingLicense,
                (msg::package_name),
                "",
                "The software license must be available at ${{CURRENT_PACKAGES_DIR}}/share/{package_name}/copyright")
DECLARE_MESSAGE(PortBugMissingReleaseBinaries, (), "", "Release binaries were not found.")
DECLARE_MESSAGE(PortBugMovePkgConfigFiles, (), "", "You can move the pkgconfig files with commands similar to:")
DECLARE_MESSAGE(PortBugOutdatedCRT, (), "", "Detected outdated dynamic CRT in the following files:")
DECLARE_MESSAGE(
    PortBugRemoveBinDir,
    (),
    "",
    "If the creation of bin\\ and/or debug\\bin\\ cannot be disabled, use this in the portfile to remove them")
DECLARE_MESSAGE(PortBugRemoveEmptyDirectories,
                (),
                "",
                "If a directory should be populated but is not, this might indicate an error in the portfile.\n"
                "If the directories are not needed and their creation cannot be disabled, use something like this in "
                "the portfile to remove them:")
DECLARE_MESSAGE(PortBugRemoveEmptyDirs,
                (),
                "Only the 'empty directories left by the above renames' part should be translated",
                "file(REMOVE_RECURSE empty directories left by the above renames)")
DECLARE_MESSAGE(PortBugRestrictedHeaderPaths,
                (),
                "A list of restricted headers is printed after this message, one per line. ",
                "The following restricted headers can prevent the core C++ runtime and other packages from "
                "compiling correctly. In exceptional circumstances, this policy can be disabled by setting CMake "
                "variable VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS in portfile.cmake.")
DECLARE_MESSAGE(PortBugSetDllsWithoutExports,
                (),
                "'exports' means an entry in a DLL's export table. After this message, one file path per line is "
                "printed listing each DLL with an empty export table.",
                "DLLs without any exports are likely a bug in the build script. If this is intended, add the "
                "following line in the portfile:\n"
                "set(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled)\n"
                "The following DLLs have no exports:")
DECLARE_MESSAGE(PortDeclaredHere,
                (msg::package_name),
                "This is printed after NoteMessage to tell the user the path on disk of a related port",
                "{package_name} is declared here")
DECLARE_MESSAGE(PortDependencyConflict,
                (msg::package_name),
                "",
                "Port {package_name} has the following unsupported dependencies:")
DECLARE_MESSAGE(PortNotInBaseline,
                (msg::package_name),
                "",
                "the baseline does not contain an entry for port {package_name}")
DECLARE_MESSAGE(PortsAdded, (msg::count), "", "The following {count} ports were added:")
DECLARE_MESSAGE(PortDoesNotExist, (msg::package_name), "", "{package_name} does not exist")
DECLARE_MESSAGE(PortMissingManifest2,
                (msg::package_name),
                "",
                "{package_name} port manifest missing (no vcpkg.json or CONTROL file)")
DECLARE_MESSAGE(PortsNoDiff, (), "", "There were no changes in the ports between the two commits.")
DECLARE_MESSAGE(PortsRemoved, (msg::count), "", "The following {count} ports were removed:")
DECLARE_MESSAGE(PortsUpdated, (msg::count), "", "The following {count} ports were updated:")
DECLARE_MESSAGE(PortSupportsField, (msg::supports_expression), "", "(supports: \"{supports_expression}\")")
DECLARE_MESSAGE(PortVersionConflict, (), "", "The following packages differ from their port versions:")
DECLARE_MESSAGE(PortVersionMultipleSpecification,
                (),
                "",
                "\"port_version\" cannot be combined with an embedded '#' in the version")
DECLARE_MESSAGE(PortVersionControlMustBeANonNegativeInteger, (), "", "\"Port-Version\" must be a non-negative integer")
DECLARE_MESSAGE(PrebuiltPackages, (), "", "There are packages that have not been built. To build them run:")
DECLARE_MESSAGE(PreviousIntegrationFileRemains, (), "", "Previous integration file was not removed.")
DECLARE_MESSAGE(ProgramReturnedNonzeroExitCode,
                (msg::tool_name, msg::exit_code),
                "The program's console output is appended after this.",
                "{tool_name} failed with exit code: ({exit_code}).")
DECLARE_MESSAGE(
    ProvideExportType,
    (),
    "",
    "At least one of the following options are required: --raw --nuget --ifw --zip --7zip --chocolatey --prefab.")
DECLARE_MESSAGE(PushingVendorFailed,
                (msg::vendor, msg::path),
                "",
                "Pushing {vendor} to \"{path}\" failed. Use --debug for more information.")
DECLARE_MESSAGE(RegistryCreated, (msg::path), "", "Successfully created registry at {path}")
DECLARE_MESSAGE(RegeneratesArtifactRegistry, (), "", "Regenerates an artifact registry")
DECLARE_MESSAGE(RegistryValueWrongType, (msg::path), "", "The registry value {path} was an unexpected type.")
DECLARE_MESSAGE(RemoveDependencies,
                (),
                "",
                "To remove dependencies in manifest mode, edit your manifest (vcpkg.json) and run 'install'.")
DECLARE_MESSAGE(RemovePackageConflict,
                (msg::package_name, msg::spec, msg::triplet),
                "",
                "{spec} is not installed, but {package_name} is installed for {triplet}. Did you mean "
                "{package_name}:{triplet}?")
DECLARE_MESSAGE(RemovingPackage,
                (msg::action_index, msg::count, msg::spec),
                "",
                "Removing {action_index}/{count} {spec}")
DECLARE_MESSAGE(ResponseFileCode,
                (),
                "Explains to the user that they can use response files on the command line, 'response_file' must "
                "have no spaces and be a legal file name.",
                "@response_file")
DECLARE_MESSAGE(RestoredPackagesFromAWS,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from AWS in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromCOS,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from COS in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromFiles,
                (msg::count, msg::elapsed, msg::path),
                "",
                "Restored {count} package(s) from {path} in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromGCS,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from GCS in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromGHA,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from GitHub Actions Cache in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromHTTP,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from HTTP servers in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(RestoredPackagesFromNuGet,
                (msg::count, msg::elapsed),
                "",
                "Restored {count} package(s) from NuGet in {elapsed}. Use --debug to see more details.")
DECLARE_MESSAGE(ResultsHeader, (), "Displayed before a list of installation results.", "RESULTS")
DECLARE_MESSAGE(ScriptAssetCacheRequiresScript,
                (),
                "",
                "expected arguments: asset config 'x-script' requires exactly the exec template as an argument")
DECLARE_MESSAGE(SecretBanner, (), "", "*** SECRET ***")
DECLARE_MESSAGE(SeeURL, (msg::url), "", "See {url} for more information.")
DECLARE_MESSAGE(SerializedBinParagraphHeader, (), "", "\nSerialized Binary Paragraph")
DECLARE_MESSAGE(SettingEnvVar,
                (msg::env_var, msg::url),
                "An example of env_var is \"HTTP(S)_PROXY\""
                "'--' at the beginning must be preserved",
                "-- Setting \"{env_var}\" environment variables to \"{url}\".")
DECLARE_MESSAGE(ShallowRepositoryDetected,
                (msg::path),
                "",
                "vcpkg was cloned as a shallow repository in: {path}\n"
                "Try again with a full vcpkg clone.")
DECLARE_MESSAGE(ShaPassedAsArgAndOption,
                (),
                "",
                "SHA512 passed as both an argument and as an option. Only pass one of these.")
DECLARE_MESSAGE(ShaPassedWithConflict,
                (),
                "",
                "SHA512 passed, but --skip-sha512 was also passed; only do one or the other.")
DECLARE_MESSAGE(SkipClearingInvalidDir,
                (msg::path),
                "",
                "Skipping clearing contents of {path} because it was not a directory.")
DECLARE_MESSAGE(SourceFieldPortNameMismatch,
                (msg::package_name, msg::path),
                "{package_name} and \"{path}\" are both names of installable ports/packages. 'Source', "
                "'CONTROL', 'vcpkg.json', and 'name' references are locale-invariant.",
                "The 'Source' field inside the CONTROL file, or \"name\" field inside the vcpkg.json "
                "file has the name {package_name} and does not match the port directory \"{path}\".")
DECLARE_MESSAGE(SpecifiedFeatureTurnedOff,
                (msg::command_name, msg::option),
                "",
                "'{command_name}' feature specifically turned off, but --{option} was specified.")
DECLARE_MESSAGE(SpecifyHostArch,
                (msg::env_var),
                "'vcpkg help triplet' is a command line that should not be localized",
                "Host triplet. See 'vcpkg help triplet' (default: {env_var})")
DECLARE_MESSAGE(SpecifyTargetArch,
                (msg::env_var),
                "'vcpkg help triplet' is a command line that should not be localized",
                "Target triplet. See 'vcpkg help triplet' (default: {env_var})")
DECLARE_MESSAGE(StartCodeUnitInContinue, (), "", "found start code unit in continue position")
DECLARE_MESSAGE(StoredBinariesToDestinations,
                (msg::count, msg::elapsed),
                "",
                "Stored binaries in {count} destinations in {elapsed}.")
DECLARE_MESSAGE(StoreOptionMissingSha, (), "", "--store option is invalid without a sha512")
DECLARE_MESSAGE(SuccessfulyExported, (msg::package_name, msg::path), "", "Exported {package_name} to {path}")
DECLARE_MESSAGE(SuggestGitPull, (), "", "The result may be outdated. Run `git pull` to get the latest results.")
DECLARE_MESSAGE(SuggestStartingBashShell,
                (),
                "",
                "Please make sure you have started a new bash shell for the change to take effect.")
DECLARE_MESSAGE(SupportedPort, (msg::package_name), "", "Port {package_name} is supported.")
DECLARE_MESSAGE(SwitchUsedMultipleTimes, (msg::option), "", "the switch '{option}' was specified multiple times")
DECLARE_MESSAGE(SynopsisHeader, (), "Printed before a description of what a command does", "Synopsis:")
DECLARE_MESSAGE(SystemApiErrorMessage,
                (msg::system_api, msg::exit_code, msg::error_msg),
                "",
                "calling {system_api} failed with {exit_code} ({error_msg})")
DECLARE_MESSAGE(SystemRootMustAlwaysBePresent,
                (),
                "",
                "Expected the SystemRoot environment variable to be always set on Windows.")
DECLARE_MESSAGE(SystemTargetsInstallFailed, (msg::path), "", "failed to install system targets file to {path}")
DECLARE_MESSAGE(ToolFetchFailed, (msg::tool_name), "", "Could not fetch {tool_name}.")
DECLARE_MESSAGE(ToolInWin10, (), "", "This utility is bundled with Windows 10 or later.")
DECLARE_MESSAGE(ToolOfVersionXNotFound,
                (msg::tool_name, msg::version),
                "",
                "A suitable version of {tool_name} was not found (required v{version}) and unable to automatically "
                "download a portable one. Please install a newer version of {tool_name}")
DECLARE_MESSAGE(ToRemovePackages,
                (msg::command_name),
                "",
                "To only remove outdated packages, run\n{command_name} remove --outdated")
DECLARE_MESSAGE(TotalInstallTime, (msg::elapsed), "", "Total install time: {elapsed}")
DECLARE_MESSAGE(ToUpdatePackages,
                (msg::command_name),
                "",
                "To update these packages and all dependencies, run\n{command_name} upgrade'")
DECLARE_MESSAGE(TrailingCommaInArray, (), "", "Trailing comma in array")
DECLARE_MESSAGE(TrailingCommaInObj, (), "", "Trailing comma in an object")
DECLARE_MESSAGE(TripletLabel, (), "", "Triplet:")
DECLARE_MESSAGE(TripletFileNotFound, (msg::triplet), "", "Triplet file {triplet}.cmake not found")
DECLARE_MESSAGE(TwoFeatureFlagsSpecified,
                (msg::value),
                "'{value}' is a feature flag.",
                "Both '{value}' and -'{value}' were specified as feature flags.")
DECLARE_MESSAGE(UnableToClearPath, (msg::path), "", "unable to delete {path}")
DECLARE_MESSAGE(UnableToReadAppDatas, (), "", "both %LOCALAPPDATA% and %APPDATA% were unreadable")
DECLARE_MESSAGE(UnableToReadEnvironmentVariable, (msg::env_var), "", "unable to read {env_var}")
DECLARE_MESSAGE(UndeterminedToolChainForTriplet,
                (msg::triplet, msg::system_name),
                "",
                "Unable to determine toolchain use for {triplet} with with CMAKE_SYSTEM_NAME {system_name}. Did "
                "you mean to use VCPKG_CHAINLOAD_TOOLCHAIN_FILE?")
DECLARE_MESSAGE(UnexpectedArgument,
                (msg::option),
                "Argument is literally what the user passed on the command line.",
                "unexpected argument: {option}")
DECLARE_MESSAGE(
    UnexpectedAssetCacheProvider,
    (),
    "",
    "unknown asset provider type: valid source types are 'x-azurl', 'x-script', 'x-block-origin', and 'clear'")
DECLARE_MESSAGE(UnexpectedByteSize,
                (msg::expected, msg::actual),
                "{expected} is the expected byte size and {actual} is the actual byte size.",
                "Expected {expected} bytes to be written, but {actual} were written.")
DECLARE_MESSAGE(UnexpectedCharExpectedCloseBrace, (), "", "Unexpected character; expected property or close brace")
DECLARE_MESSAGE(UnexpectedCharExpectedColon, (), "", "Unexpected character; expected colon")
DECLARE_MESSAGE(UnexpectedCharExpectedName, (), "", "Unexpected character; expected property name")
DECLARE_MESSAGE(UnexpectedCharExpectedValue, (), "", "Unexpected character; expected value")
DECLARE_MESSAGE(UnexpectedCharMidArray, (), "", "Unexpected character in middle of array")
DECLARE_MESSAGE(UnexpectedCharMidKeyword, (), "", "Unexpected character in middle of keyword")
DECLARE_MESSAGE(UnexpectedDigitsAfterLeadingZero, (), "", "Unexpected digits after a leading zero")
DECLARE_MESSAGE(UnexpectedEOFAfterBacktick, (), "", "unexpected eof: trailing unescaped backticks (`) are not allowed")
DECLARE_MESSAGE(UnexpectedEOFAfterEscape, (), "", "Unexpected EOF after escape character")
DECLARE_MESSAGE(UnexpectedEOFAfterMinus, (), "", "Unexpected EOF after minus sign")
DECLARE_MESSAGE(UnexpectedEOFExpectedChar, (), "", "Unexpected character; expected EOF")
DECLARE_MESSAGE(UnexpectedEOFExpectedCloseBrace, (), "", "Unexpected EOF; expected property or close brace")
DECLARE_MESSAGE(UnexpectedEOFExpectedColon, (), "", "Unexpected EOF; expected colon")
DECLARE_MESSAGE(UnexpectedEOFExpectedName, (), "", "Unexpected EOF; expected property name")
DECLARE_MESSAGE(UnexpectedEOFExpectedProp, (), "", "Unexpected EOF; expected property")
DECLARE_MESSAGE(UnexpectedEOFExpectedValue, (), "", "Unexpected EOF; expected value")
DECLARE_MESSAGE(UnexpectedEOFMidArray, (), "", "Unexpected EOF in middle of array")
DECLARE_MESSAGE(UnexpectedEOFMidKeyword, (), "", "Unexpected EOF in middle of keyword")
DECLARE_MESSAGE(UnexpectedEOFMidString, (), "", "Unexpected EOF in middle of string")
DECLARE_MESSAGE(UnexpectedEOFMidUnicodeEscape, (), "", "Unexpected end of file in middle of unicode escape")
DECLARE_MESSAGE(UnexpectedEscapeSequence, (), "", "Unexpected escape sequence continuation")
DECLARE_MESSAGE(UnexpectedField, (msg::json_field), "", "unexpected field '{json_field}'")
DECLARE_MESSAGE(UnexpectedFieldSuggest,
                (msg::json_field, msg::value),
                "{value} is a suggested field name to use in a JSON document",
                "unexpected field '{json_field}', did you mean '{value}'?")
DECLARE_MESSAGE(UnexpectedFormat,
                (msg::expected, msg::actual),
                "{expected} is the expected format, {actual} is the actual format.",
                "Expected format is [{expected}], but was [{actual}].")
DECLARE_MESSAGE(UnexpectedOption,
                (msg::option),
                "Option is a command line option like --option=value",
                "unexpected option: {option}")
DECLARE_MESSAGE(UnexpectedPortName,
                (msg::expected, msg::actual, msg::path),
                "{expected} is the expected port and {actual} is the port declared by the user.",
                "the port {expected} is declared as {actual} in {path}")
DECLARE_MESSAGE(UnexpectedPortversion,
                (),
                "'field' means a JSON key/value pair here",
                "unexpected \"port-version\" without a versioning field")
DECLARE_MESSAGE(UnexpectedSwitch,
                (msg::option),
                "Switch is a command line switch like --switch",
                "unexpected switch: {option}")
DECLARE_MESSAGE(UnexpectedToolOutput,
                (msg::tool_name, msg::path),
                "The actual command line output will be appended after this message.",
                "{tool_name} ({path}) produced unexpected output when attempting to determine the version:")
DECLARE_MESSAGE(UnexpectedWindowsArchitecture,
                (msg::actual),
                "{actual} is the CPU kind we observed like ARM or MIPS",
                "unexpected Windows host architecture: {actual}")
DECLARE_MESSAGE(UnknownBaselineFileContent,
                (),
                "",
                "unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'")
DECLARE_MESSAGE(UnknownBinaryProviderType,
                (),
                "",
                "unknown binary provider type: valid providers are 'clear', 'default', 'nuget', "
                "'nugetconfig', 'nugettimeout', 'interactive', 'x-azblob', 'x-gcs', 'x-aws', "
                "'x-aws-config', 'http', and 'files'")
DECLARE_MESSAGE(UnknownBooleanSetting,
                (msg::option, msg::value),
                "{value} is what {option} is set to",
                "unknown boolean setting for {option}: \"{value}\". Valid values are '', '1', '0', 'ON', 'OFF', "
                "'TRUE', and 'FALSE'.")
DECLARE_MESSAGE(UnknownParameterForIntegrate,
                (msg::value),
                "'{value}' is a user-supplied command line option. For example, given vcpkg integrate frobinate, "
                "{value} would be frobinate.",
                "Unknown parameter '{value}' for integrate.")
DECLARE_MESSAGE(UnknownPolicySetting,
                (msg::option, msg::value),
                "'{value}' is the policy in question. These are unlocalized names that ports use to control post "
                "build checks. Some examples are VCPKG_POLICY_DLLS_WITHOUT_EXPORTS, "
                "VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES, or VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT",
                "Unknown setting for policy '{value}': {option}")
DECLARE_MESSAGE(UnknownSettingForBuildType,
                (msg::option),
                "",
                "Unknown setting for VCPKG_BUILD_TYPE {option}. Valid settings are '', 'debug', and 'release'.")
DECLARE_MESSAGE(UnknownTool, (), "", "vcpkg does not have a definition of this tool for this platform.")
DECLARE_MESSAGE(UnknownTopic,
                (msg::value),
                "{value} the value a user passed to `vcpkg help` that we don't understand",
                "unknown topic {value}")
DECLARE_MESSAGE(UnknownVariablesInTemplate,
                (msg::value, msg::list),
                "{value} is the value provided by the user and {list} a list of unknown variables seperated by comma",
                "invalid argument: url template '{value}' contains unknown variables: {list}")
DECLARE_MESSAGE(UnrecognizedConfigField, (), "", "configuration contains the following unrecognized fields:")
DECLARE_MESSAGE(UnrecognizedIdentifier,
                (msg::value),
                "'{value}' is an expression identifier. For example, given an expression 'windows & x86', "
                "'windows' and 'x86' are identifiers.",
                "Unrecognized identifer name {value}. Add to override list in triplet file.")
DECLARE_MESSAGE(UnsupportedFeature,
                (msg::feature, msg::package_name),
                "",
                "feature {feature} was passed, but that is not a feature supported by {package_name} supports.")
DECLARE_MESSAGE(UnsupportedFeatureSupportsExpression,
                (msg::package_name, msg::feature_spec, msg::supports_expression, msg::triplet),
                "",
                "{feature_spec} is only supported on '{supports_expression}', "
                "which does not match {triplet}. This usually means that there are known "
                "build failures, or runtime problems, when building other platforms. To ignore this and attempt to "
                "build {package_name} anyway, rerun vcpkg with `--allow-unsupported`.")
DECLARE_MESSAGE(UnsupportedFeatureSupportsExpressionWarning,
                (msg::feature_spec, msg::supports_expression, msg::triplet),
                "",
                "{feature_spec} is only supported on '{supports_expression}', "
                "which does not match {triplet}. This usually means that there are known build failures, "
                "or runtime problems, when building other platforms. Proceeding anyway due to `--allow-unsupported`.")
DECLARE_MESSAGE(UnsupportedPort, (msg::package_name), "", "Port {package_name} is not supported.")
DECLARE_MESSAGE(UnsupportedPortDependency,
                (msg::value),
                "'{value}' is the name of a port dependency.",
                "- dependency {value} is not supported.")
DECLARE_MESSAGE(UnsupportedSyntaxInCDATA, (), "", "]]> is not supported in CDATA block")
DECLARE_MESSAGE(UnsupportedSystemName,
                (msg::system_name),
                "",
                "Could not map VCPKG_CMAKE_SYSTEM_NAME '{system_name}' to a vcvarsall platform. "
                "Supported system names are '', 'Windows' and 'WindowsStore'.")
DECLARE_MESSAGE(UnsupportedToolchain,
                (msg::triplet, msg::arch, msg::path, msg::list),
                "example for {list} is 'x86, arm64'",
                "in triplet {triplet}: Unable to find a valid toolchain for requested target architecture {arch}.\n"
                "The selected Visual Studio instance is at: {path}\n"
                "The available toolchain combinations are: {list}")
DECLARE_MESSAGE(UnsupportedUpdateCMD,
                (),
                "",
                "the update command does not currently support manifest mode. Instead, modify your vcpkg.json and "
                "run install.")
DECLARE_MESSAGE(UpdateBaselineAddBaselineNoManifest,
                (msg::option),
                "",
                "the --{option} switch was passed, but there is no manifest file to add a `builtin-baseline` field to.")
DECLARE_MESSAGE(UpdateBaselineLocalGitError,
                (msg::path),
                "",
                "git failed to parse HEAD for the local vcpkg registry at \"{path}\"")
DECLARE_MESSAGE(UpdateBaselineNoConfiguration,
                (),
                "",
                "neither `vcpkg.json` nor `vcpkg-configuration.json` exist to update.")
DECLARE_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline,
                (msg::option),
                "",
                "the manifest file currently does not contain a `builtin-baseline` field; in order to "
                "add one, pass the --{option} switch.")
DECLARE_MESSAGE(UpdateBaselineNoUpdate,
                (msg::url, msg::value),
                "example of {value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                "registry '{url}' not updated: '{value}'")
DECLARE_MESSAGE(UpdateBaselineRemoteGitError, (msg::url), "", "git failed to fetch remote repository '{url}'")
DECLARE_MESSAGE(UpdateBaselineUpdatedBaseline,
                (msg::url, msg::old_value, msg::new_value),
                "example of {old_value}, {new_value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                "updated registry '{url}': baseline '{old_value}' -> '{new_value}'")
DECLARE_MESSAGE(
    UpgradeInManifest,
    (),
    "'vcpkg x-update-baseline' and 'vcpkg install' are command lines and should not be localized.",
    "Upgrade upgrades a classic mode installation and thus does not support manifest mode. Consider updating your "
    "dependencies by updating your baseline to a current value with vcpkg x-update-baseline and running vcpkg install.")
DECLARE_MESSAGE(
    UpgradeRunWithNoDryRun,
    (),
    "",
    "If you are sure you want to rebuild the above packages, run this command with the --no-dry-run option.")
DECLARE_MESSAGE(UploadingBinariesToVendor,
                (msg::spec, msg::vendor, msg::path),
                "",
                "Uploading binaries for '{spec}' to '{vendor}' source \"{path}\".")
DECLARE_MESSAGE(UseEnvVar,
                (msg::env_var),
                "An example of env_var is \"HTTP(S)_PROXY\""
                "'--' at the beginning must be preserved",
                "-- Using {env_var} in environment variables.")
DECLARE_MESSAGE(UserWideIntegrationDeleted, (), "", "User-wide integration is not installed.")
DECLARE_MESSAGE(UserWideIntegrationRemoved, (), "", "User-wide integration was removed.")
DECLARE_MESSAGE(UsingManifestAt, (msg::path), "", "Using manifest file at {path}.")
DECLARE_MESSAGE(Utf8ConversionFailed, (), "", "Failed to convert to UTF-8")
DECLARE_MESSAGE(VcpkgCeIsExperimental,
                (),
                "The name of the feature is 'vcpkg-artifacts' and should be singular despite ending in s",
                "vcpkg-artifacts is experimental and may change at any time.")
DECLARE_MESSAGE(
    VcpkgCompletion,
    (msg::value, msg::path),
    "'{value}' is the subject for completion. i.e. bash, zsh, etc.",
    "vcpkg {value} completion is already imported to your \"{path}\" file.\nThe following entries were found:")
DECLARE_MESSAGE(VcpkgDisallowedClassicMode,
                (),
                "",
                "Could not locate a manifest (vcpkg.json) above the current working "
                "directory.\nThis vcpkg distribution does not have a classic mode instance.")
DECLARE_MESSAGE(
    VcpkgHasCrashed,
    (),
    "Printed at the start of a crash report.",
    "vcpkg has crashed. Please create an issue at https://github.com/microsoft/vcpkg containing a brief summary of "
    "what you were trying to do and the following information.")
DECLARE_MESSAGE(VcpkgInvalidCommand, (msg::command_name), "", "invalid command: {command_name}")
DECLARE_MESSAGE(VcpkgUsage,
                (),
                "This is describing a command line, everything should be localized except 'vcpkg'; symbols like <>s, "
                "[]s, or --s should be preserved. @response_file should be localized to be consistent with the message "
                "named 'ResponseFileCode'.",
                "usage: vcpkg <command> [--switches] [--options=values] [arguments] @response_file")
DECLARE_MESSAGE(InvalidUri, (msg::value), "{value} is the URI we attempted to parse.", "unable to parse uri: {value}")
DECLARE_MESSAGE(VcpkgInVsPrompt,
                (msg::value, msg::triplet),
                "'{value}' is a VS prompt",
                "vcpkg appears to be in a Visual Studio prompt targeting {value} but installing for {triplet}. "
                "Consider using --triplet {value}-windows or --triplet {value}-uwp.")
DECLARE_MESSAGE(VcpkgRegistriesCacheIsNotDirectory,
                (msg::path),
                "",
                "Value of environment variable X_VCPKG_REGISTRIES_CACHE is not a directory: {path}")
DECLARE_MESSAGE(VcpkgRootRequired, (), "", "Setting VCPKG_ROOT is required for standalone bootstrap.")
DECLARE_MESSAGE(VcpkgRootsDir, (msg::env_var), "", "The vcpkg root directory (default: {env_var})")
DECLARE_MESSAGE(VcpkgSendMetricsButDisabled, (), "", "passed --sendmetrics, but metrics are disabled.")
DECLARE_MESSAGE(VcvarsRunFailed, (), "", "failed to run vcvarsall.bat to get a Visual Studio environment")
DECLARE_MESSAGE(VcvarsRunFailedExitCode,
                (msg::exit_code),
                "",
                "while trying to get a Visual Studio environment, vcvarsall.bat returned {exit_code}")
DECLARE_MESSAGE(VersionBaselineMatch, (msg::version_spec), "", "{version_spec} matches the current baseline")
DECLARE_MESSAGE(VersionBaselineMismatch,
                (msg::expected, msg::actual, msg::package_name),
                "{actual} and {expected} are versions",
                "{package_name} is assigned {actual}, but the local port is {expected}")
DECLARE_MESSAGE(VersionBuiltinPortTreeEntryMissing,
                (msg::package_name, msg::expected, msg::actual),
                "{expected} and {actual} are versions like 1.0.",
                "no version database entry for {package_name} at {expected}; using the checked out ports tree "
                "version ({actual}).")
DECLARE_MESSAGE(VersionCommandHeader,
                (msg::version),
                "",
                "vcpkg package management program version {version}\n\nSee LICENSE.txt for license information.")
DECLARE_MESSAGE(
    VersionConflictXML,
    (msg::path, msg::expected_version, msg::actual_version),
    "",
    "Expected {path} version: [{expected_version}], but was [{actual_version}]. Please re-run bootstrap-vcpkg.")
DECLARE_MESSAGE(VersionConstraintNotInDatabase1,
                (msg::package_name, msg::version),
                "",
                "the \"version>=\" constraint to {package_name} names version {version} which does not exist in the "
                "version database. All versions must exist in the version database to be interpreted by vcpkg.")
DECLARE_MESSAGE(VersionConstraintNotInDatabase2,
                (),
                "",
                "consider removing the version constraint or choosing a value declared here")
DECLARE_MESSAGE(VersionConstraintOk, (), "", "all version constraints are consistent with the version database")
DECLARE_MESSAGE(VersionConstraintPortVersionMustBePositiveInteger,
                (),
                "",
                "port-version (after the '#') in \"version>=\" must be a non-negative integer")
DECLARE_MESSAGE(VersionConstraintViolated,
                (msg::spec, msg::expected_version, msg::actual_version),
                "",
                "dependency {spec} was expected to be at least version "
                "{expected_version}, but is currently {actual_version}.")
DECLARE_MESSAGE(VersionDatabaseFileMissing, (), "", "this port is not in the version database")
DECLARE_MESSAGE(VersionDatabaseFileMissing2, (), "", "the version database file should be here")
DECLARE_MESSAGE(VersionDatabaseFileMissing3,
                (msg::command_line),
                "",
                "run '{command_line}' to create the version database file")
DECLARE_MESSAGE(VersionDatabaseEntryMissing,
                (msg::package_name, msg::version),
                "",
                "no version entry for {package_name} at {version}.")
DECLARE_MESSAGE(VersionGitEntryMissing,
                (msg::package_name, msg::version),
                "A list of versions, 1 per line, are printed after this message.",
                "no version database entry for {package_name} at {version}.\nAvailable versions:")
DECLARE_MESSAGE(VersionIncomparable1,
                (msg::spec, msg::constraint_origin, msg::expected, msg::actual),
                "{expected} and {actual} are versions like 1.0",
                "version conflict on {spec}: {constraint_origin} required {expected}, which cannot be compared with "
                "the baseline version {actual}.")
DECLARE_MESSAGE(VersionIncomparableSchemeString, (), "", "Both versions have scheme string but different primary text.")
DECLARE_MESSAGE(VersionIncomparableSchemes, (), "", "The versions have incomparable schemes:")
DECLARE_MESSAGE(VersionIncomparable2,
                (msg::version_spec, msg::new_scheme),
                "",
                "{version_spec} has scheme {new_scheme}")
DECLARE_MESSAGE(VersionIncomparable3,
                (),
                "This precedes a JSON document describing the fix",
                "This can be resolved by adding an explicit override to the preferred version. For example:")
DECLARE_MESSAGE(VersionIncomparable4, (msg::url), "", "See `vcpkg help versioning` or {url} for more information.")
DECLARE_MESSAGE(VersionInDeclarationDoesNotMatch,
                (msg::git_tree_sha, msg::expected, msg::actual),
                "{expected} and {actual} are version specs",
                "{git_tree_sha} is declared to contain {expected}, but appears to contain {actual}")
DECLARE_MESSAGE(
    VersionInvalidDate,
    (msg::version),
    "",
    "`{version}` is not a valid date version. Dates must follow the format YYYY-MM-DD and disambiguators must be "
    "dot-separated positive integer values without leading zeroes.")
DECLARE_MESSAGE(VersionInvalidRelaxed,
                (msg::version),
                "",
                "`{version}` is not a valid relaxed version (semver with arbitrary numeric element count).")
DECLARE_MESSAGE(VersionInvalidSemver,
                (msg::version),
                "",
                "`{version}` is not a valid semantic version, consult <https://semver.org>.")
DECLARE_MESSAGE(
    VersionMissing,
    (),
    "The names version, version-date, version-semver, and version-string are code and must not be localized",
    "expected a versioning field (one of version, version-date, version-semver, or version-string)")
DECLARE_MESSAGE(VersionMissingRequiredFeature,
                (msg::version_spec, msg::feature, msg::constraint_origin),
                "",
                "{version_spec} does not have required feature {feature} needed by {constraint_origin}")
DECLARE_MESSAGE(VersionNotFound,
                (msg::expected, msg::actual),
                "{expected} and {actual} are versions",
                "{expected} not available, only {actual} is available")
DECLARE_MESSAGE(VersionNotFoundInVersionsFile2,
                (msg::version_spec),
                "",
                "{version_spec} was not found in versions database")
DECLARE_MESSAGE(VersionNotFoundInVersionsFile3, (), "", "the version should be in this file")
DECLARE_MESSAGE(VersionNotFoundInVersionsFile4,
                (msg::command_line),
                "",
                "run '{command_line}' to add the new port version")
DECLARE_MESSAGE(VersionOverrideNotInVersionDatabase,
                (msg::package_name),
                "",
                "the version override {package_name} does not exist in the version database; does that port exist?")
DECLARE_MESSAGE(
    VersionOverrideVersionNotInVersionDatabase1,
    (msg::package_name, msg::version),
    "",
    "the override of {package_name} names version {version} which does not exist in the "
    "version database. Installing this port at the top level will fail as that version will be unresolvable.")
DECLARE_MESSAGE(VersionOverrideVersionNotInVersionDatabase2,
                (),
                "",
                "consider removing the version override or choosing a value declared here")
DECLARE_MESSAGE(VersionOverwriteVersion,
                (msg::version_spec),
                "",
                "you can overwrite {version_spec} with correct local values by running:")
DECLARE_MESSAGE(VersionRejectedDueToBaselineMissing,
                (msg::path, msg::json_field),
                "",
                "{path} was rejected because it uses \"{json_field}\" and does not have a \"builtin-baseline\". "
                "This can be fixed by removing the uses of \"{json_field}\" or adding a \"builtin-baseline\".\nSee "
                "`vcpkg help versioning` for more information.")
DECLARE_MESSAGE(VersionRejectedDueToFeatureFlagOff,
                (msg::path, msg::json_field),
                "",
                "{path} was rejected because it uses \"{json_field}\" and the `versions` feature flag is disabled. "
                "This can be fixed by removing \"{json_field}\" or enabling the `versions` feature flag.\nSee "
                "`vcpkg help versioning` for more information.")
DECLARE_MESSAGE(VersionSchemeMismatch1,
                (msg::version, msg::expected, msg::actual, msg::package_name),
                "{expected} and {actual} are version schemes; it here refers to the {version}",
                "{version} is declared {expected}, but {package_name} is declared with {actual}")
DECLARE_MESSAGE(VersionSchemeMismatch1Old,
                (msg::version, msg::expected, msg::actual, msg::package_name, msg::git_tree_sha),
                "{expected} and {actual} are version schemes; it here refers to the {version}",
                "{version} is declared {expected}, but {package_name}@{git_tree_sha} is declared with {actual}")
DECLARE_MESSAGE(VersionSchemeMismatch2,
                (),
                "",
                "versions must be unique, even if they are declared with different schemes")
DECLARE_MESSAGE(VersionShaMismatch1,
                (msg::version_spec, msg::git_tree_sha),
                "'git tree' is ",
                "{version_spec} git tree {git_tree_sha} does not match the port directory")
DECLARE_MESSAGE(VersionShaMismatch2, (msg::git_tree_sha), "", "the port directory has git tree {git_tree_sha}")
DECLARE_MESSAGE(VersionShaMismatch3,
                (msg::version_spec),
                "",
                "if {version_spec} is already published, update this file with a new version or port-version, commit "
                "it, then add the new version by running:")
DECLARE_MESSAGE(VersionShaMismatch4,
                (msg::version_spec),
                "",
                "if {version_spec} is not yet published, overwrite the previous git tree by running:")
DECLARE_MESSAGE(
    VersionShaMissing1,
    (),
    "",
    "the git tree of the port directory could not be determined. This is usually caused by uncommitted changes.")
DECLARE_MESSAGE(VersionShaMissing2,
                (),
                "",
                "you can commit your changes and add them to the version database by running:")
DECLARE_MESSAGE(VersionShaMissing3,
                (),
                "This is short for 'work in progress' and must be enclosed in \" quotes if it is more than 1 word",
                "wip")
DECLARE_MESSAGE(VersionShaMissing4, (msg::package_name), "", "[{package_name}] Add new port")
DECLARE_MESSAGE(VersionSharpMustBeFollowedByPortVersion,
                (),
                "",
                "'#' in version text must be followed by a port version")
DECLARE_MESSAGE(VersionSharpMustBeFollowedByPortVersionNonNegativeInteger,
                (),
                "",
                "'#' in version text must be followed by a port version (a non-negative integer)")
DECLARE_MESSAGE(VersionSpecMismatch,
                (msg::path, msg::expected_version, msg::actual_version),
                "",
                "Failed to load port because versions are inconsistent. The file \"{path}\" contains the version "
                "{actual_version}, but the version database indicates that it should be {expected_version}.")
DECLARE_MESSAGE(VersionVerifiedOK,
                (msg::version_spec, msg::git_tree_sha),
                "",
                "{version_spec} is correctly in the version database ({git_tree_sha})")
DECLARE_MESSAGE(VSExaminedInstances, (), "", "The following Visual Studio instances were considered:")
DECLARE_MESSAGE(VSExaminedPaths, (), "", "The following paths were examined for Visual Studio instances:")
DECLARE_MESSAGE(VSNoInstances, (), "", "Could not locate a complete Visual Studio instance")
DECLARE_MESSAGE(WaitingForChildrenToExit, (), "", "Waiting for child processes to exit...")
DECLARE_MESSAGE(WaitingToTakeFilesystemLock, (msg::path), "", "waiting to take filesystem lock on {path}...")
DECLARE_MESSAGE(WarningsTreatedAsErrors, (), "", "previous warnings being interpreted as errors")
DECLARE_MESSAGE(WarnOnParseConfig, (msg::path), "", "Found the following warnings in configuration {path}:")
DECLARE_MESSAGE(WhileCheckingOutBaseline, (msg::commit_sha), "", "while checking out baseline {commit_sha}")
DECLARE_MESSAGE(WhileCheckingOutPortTreeIsh,
                (msg::package_name, msg::git_tree_sha),
                "",
                "while checking out port {package_name} with git tree {git_tree_sha}")
DECLARE_MESSAGE(WhileGettingLocalTreeIshObjectsForPorts, (), "", "while getting local treeish objects for ports")
DECLARE_MESSAGE(WhileLookingForSpec, (msg::spec), "", "while looking for {spec}:")
DECLARE_MESSAGE(WhileLoadingBaselineVersionForPort,
                (msg::package_name),
                "",
                "while loading baseline version for {package_name}")
DECLARE_MESSAGE(WhileLoadingPortVersion, (msg::version_spec), "", "while loading {version_spec}")
DECLARE_MESSAGE(WhileParsingVersionsForPort,
                (msg::package_name, msg::path),
                "",
                "while parsing versions for {package_name} from {path}")
DECLARE_MESSAGE(WhileValidatingVersion, (msg::version), "", "while validating version: {version}")
DECLARE_MESSAGE(WindowsOnlyCommand, (), "", "This command only supports Windows.")
DECLARE_MESSAGE(WroteNuGetPkgConfInfo, (msg::path), "", "Wrote NuGet package config information to {path}")
