#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    // These constants are contractual values that need to agree across the codebase, so they're all declared here to
    // avoid typos.

    // JSON IDs are lowercase separated by dashes
    inline constexpr StringLiteral JsonIdAbi = "abi";
    inline constexpr StringLiteral JsonIdAcquiredArtifacts = "acquired-artifacts";
    inline constexpr StringLiteral JsonIdActivatedArtifacts = "activated-artifacts";
    inline constexpr StringLiteral JsonIdAlgorithm = "algorithm";
    inline constexpr StringLiteral JsonIdAllCapsSHA256 = "SHA256";
    inline constexpr StringLiteral JsonIdAllCapsSHA512 = "SHA512";
    inline constexpr StringLiteral JsonIdApply = "apply";
    inline constexpr StringLiteral JsonIdArchiveCapitalLocation = "archiveLocation";
    inline constexpr StringLiteral JsonIdArtifact = "artifact";
    inline constexpr StringLiteral JsonIdBaseline = "baseline";
    inline constexpr StringLiteral JsonIdBuildtrees = "buildtrees";
    inline constexpr StringLiteral JsonIdBuiltin = "builtin";
    inline constexpr StringLiteral JsonIdBuiltinBaseline = "builtin-baseline";
    inline constexpr StringLiteral JsonIdBuiltinError = "builtin-error";
    inline constexpr StringLiteral JsonIdBuiltinFiles = "builtin-files";
    inline constexpr StringLiteral JsonIdBuiltinGit = "builtin-git";
    inline constexpr StringLiteral JsonIdCacheCapitalId = "cacheId";
    inline constexpr StringLiteral JsonIdCacheCapitalSize = "cacheSize";
    inline constexpr StringLiteral JsonIdChecksums = "checksums";
    inline constexpr StringLiteral JsonIdComment = "comment";
    inline constexpr StringLiteral JsonIdContacts = "contacts";
    inline constexpr StringLiteral JsonIdCorrelator = "correlator";
    inline constexpr StringLiteral JsonIdCreated = "created";
    inline constexpr StringLiteral JsonIdCreators = "creators";
    inline constexpr StringLiteral JsonIdDefault = "default";
    inline constexpr StringLiteral JsonIdDefaultFeatures = "default-features";
    inline constexpr StringLiteral JsonIdDefaultRegistry = "default-registry";
    inline constexpr StringLiteral JsonIdDefaultTriplet = "default-triplet";
    inline constexpr StringLiteral JsonIdDemands = "demands";
    inline constexpr StringLiteral JsonIdDependencies = "dependencies";
    inline constexpr StringLiteral JsonIdDescription = "description";
    inline constexpr StringLiteral JsonIdDetectedCIEnvironment = "detected-ci-environment";
    inline constexpr StringLiteral JsonIdDetector = "detector";
    inline constexpr StringLiteral JsonIdDirect = "direct";
    inline constexpr StringLiteral JsonIdDocumentation = "documentation";
    inline constexpr StringLiteral JsonIdDollarSchema = "$schema";
    inline constexpr StringLiteral JsonIdDownloads = "downloads";
    inline constexpr StringLiteral JsonIdError = "error";
    inline constexpr StringLiteral JsonIdFeatures = "features";
    inline constexpr StringLiteral JsonIdFiles = "files";
    inline constexpr StringLiteral JsonIdFilesystem = "filesystem";
    inline constexpr StringLiteral JsonIdGit = "git";
    inline constexpr StringLiteral JsonIdGitTree = "git-tree";
    inline constexpr StringLiteral JsonIdHomepage = "homepage";
    inline constexpr StringLiteral JsonIdHost = "host";
    inline constexpr StringLiteral JsonIdHostTriplet = "host-triplet";
    inline constexpr StringLiteral JsonIdId = "id";
    inline constexpr StringLiteral JsonIdInstalled = "installed";
    inline constexpr StringLiteral JsonIdJob = "job";
    inline constexpr StringLiteral JsonIdKey = "key";
    inline constexpr StringLiteral JsonIdKind = "kind";
    inline constexpr StringLiteral JsonIdLicense = "license";
    inline constexpr StringLiteral JsonIdLocation = "location";
    inline constexpr StringLiteral JsonIdMaintainers = "maintainers";
    inline constexpr StringLiteral JsonIdManifestModeEnabled = "manifest-mode-enabled";
    inline constexpr StringLiteral JsonIdManifestRoot = "manifest-root";
    inline constexpr StringLiteral JsonIdManifests = "manifests";
    inline constexpr StringLiteral JsonIdMessage = "message";
    inline constexpr StringLiteral JsonIdMicrosoft = "microsoft";
    inline constexpr StringLiteral JsonIdName = "name";
    inline constexpr StringLiteral JsonIdOverlayPorts = "overlay-ports";
    inline constexpr StringLiteral JsonIdOverlayTriplets = "overlay-triplets";
    inline constexpr StringLiteral JsonIdOverrides = "overrides";
    inline constexpr StringLiteral JsonIdPackages = "packages";
    inline constexpr StringLiteral JsonIdPackageUnderscoreName = "package_name";
    inline constexpr StringLiteral JsonIdPackageUnderscoreUrl = "package_url";
    inline constexpr StringLiteral JsonIdPath = "path";
    inline constexpr StringLiteral JsonIdPlatform = "platform";
    inline constexpr StringLiteral JsonIdPortUnderscoreVersion = "port_version";
    inline constexpr StringLiteral JsonIdPortVersion = "port-version";
    inline constexpr StringLiteral JsonIdRef = "ref";
    inline constexpr StringLiteral JsonIdReference = "reference";
    inline constexpr StringLiteral JsonIdRegistries = "registries";
    inline constexpr StringLiteral JsonIdRelationship = "relationship";
    inline constexpr StringLiteral JsonIdRelationships = "relationships";
    inline constexpr StringLiteral JsonIdRepository = "repository";
    inline constexpr StringLiteral JsonIdRequires = "requires";
    inline constexpr StringLiteral JsonIdResolved = "resolved";
    inline constexpr StringLiteral JsonIdScanned = "scanned";
    inline constexpr StringLiteral JsonIdSettings = "settings";
    inline constexpr StringLiteral JsonIdSha = "sha";
    inline constexpr StringLiteral JsonIdState = "state";
    inline constexpr StringLiteral JsonIdSummary = "summary";
    inline constexpr StringLiteral JsonIdSupports = "supports";
    inline constexpr StringLiteral JsonIdTools = "tools";
    inline constexpr StringLiteral JsonIdTriplet = "triplet";
    inline constexpr StringLiteral JsonIdUrl = "url";
    inline constexpr StringLiteral JsonIdVcpkgAssetSources = "vcpkg-asset-sources";
    inline constexpr StringLiteral JsonIdVcpkgConfiguration = "vcpkg-configuration";
    inline constexpr StringLiteral JsonIdVcpkgDisableMetrics = "vcpkg-disable-metrics";
    inline constexpr StringLiteral JsonIdVcpkgDotJson = "vcpkg.json";
    inline constexpr StringLiteral JsonIdVcpkgDownloads = "vcpkg-downloads";
    inline constexpr StringLiteral JsonIdVcpkgRoot = "vcpkg-root";
    inline constexpr StringLiteral JsonIdVcpkgRootArg = "vcpkg-root-arg";
    inline constexpr StringLiteral JsonIdVcpkgRootEnv = "vcpkg-root-env";
    inline constexpr StringLiteral JsonIdVersion = "version";
    inline constexpr StringLiteral JsonIdVersionDate = "version-date";
    inline constexpr StringLiteral JsonIdVersionGreaterEqual = "version>=";
    inline constexpr StringLiteral JsonIdVersions = "versions";
    inline constexpr StringLiteral JsonIdVersionSemver = "version-semver";
    inline constexpr StringLiteral JsonIdVersionsOutput = "versions-output";
    inline constexpr StringLiteral JsonIdVersionString = "version-string";
    inline constexpr StringLiteral JsonIdWarning = "warning";

    // SPDX constants are JsonIds which follow capitalization and separation in the SPDX specification,
    // rather than the lowercase-dash convention used above.
    //
    // SPDX documents also use the JsonId constants above for those values consistent with those we
    // would use in other contexts.
    inline constexpr StringLiteral SpdxCCZero = "CC0-1.0";
    inline constexpr StringLiteral SpdxChecksumValue = "checksumValue";
    inline constexpr StringLiteral SpdxContainedBy = "CONTAINED_BY";
    inline constexpr StringLiteral SpdxContains = "CONTAINS";
    inline constexpr StringLiteral SpdxCopyrightText = "copyrightText";
    inline constexpr StringLiteral SpdxCreationInfo = "creationInfo";
    inline constexpr StringLiteral SpdxDataLicense = "dataLicense";
    inline constexpr StringLiteral SpdxDependencyManifestOf = "DEPENDENCY_MANIFEST_OF";
    inline constexpr StringLiteral SpdxDocumentNamespace = "documentNamespace";
    inline constexpr StringLiteral SpdxDownloadLocation = "downloadLocation";
    inline constexpr StringLiteral SpdxElementId = "spdxElementId";
    inline constexpr StringLiteral SpdxFileName = "fileName";
    inline constexpr StringLiteral SpdxGeneratedFrom = "GENERATED_FROM";
    inline constexpr StringLiteral SpdxGenerates = "GENERATES";
    inline constexpr StringLiteral SpdxLicenseConcluded = "licenseConcluded";
    inline constexpr StringLiteral SpdxLicenseDeclared = "licenseDeclared";
    inline constexpr StringLiteral SpdxNoAssertion = "NOASSERTION";
    inline constexpr StringLiteral SpdxNone = "NONE";
    inline constexpr StringLiteral SpdxPackageFileName = "packageFileName";
    inline constexpr StringLiteral SpdxRefBinary = "SPDXRef-binary";
    inline constexpr StringLiteral SpdxRefDocument = "SPDXRef-DOCUMENT";
    inline constexpr StringLiteral SpdxRefPort = "SPDXRef-port";
    inline constexpr StringLiteral SpdxRelatedSpdxElement = "relatedSpdxElement";
    inline constexpr StringLiteral SpdxRelationshipType = "relationshipType";
    inline constexpr StringLiteral SpdxSpdxId = "SPDXID";
    inline constexpr StringLiteral SpdxTwoTwo = "SPDX-2.2";
    inline constexpr StringLiteral SpdxVersion = "spdxVersion";
    inline constexpr StringLiteral SpdxVersionInfo = "versionInfo";

    // Paragraph IDs are *usually* Capitals-Separated-By-Dashes
    inline constexpr StringLiteral ParagraphIdAbi = "Abi";
    inline constexpr StringLiteral ParagraphIdArchitecture = "Architecture";
    inline constexpr StringLiteral ParagraphIdBuildDepends = "Build-Depends";
    inline constexpr StringLiteral ParagraphIdCrtLinkage = "CRTLinkage";
    inline constexpr StringLiteral ParagraphIdDefaultFeatures = "Default-Features";
    inline constexpr StringLiteral ParagraphIdDepends = "Depends";
    inline constexpr StringLiteral ParagraphIdDescription = "Description";
    inline constexpr StringLiteral ParagraphIdFeature = "Feature";
    inline constexpr StringLiteral ParagraphIdHomepage = "Homepage";
    inline constexpr StringLiteral ParagraphIdLibraryLinkage = "LibraryLinkage";
    inline constexpr StringLiteral ParagraphIdMaintainer = "Maintainer";
    inline constexpr StringLiteral ParagraphIdMultiArch = "Multi-Arch";
    inline constexpr StringLiteral ParagraphIdPackage = "Package";
    inline constexpr StringLiteral ParagraphIdPortVersion = "Port-Version";
    inline constexpr StringLiteral ParagraphIdSource = "Source";
    inline constexpr StringLiteral ParagraphIdSupports = "Supports";
    inline constexpr StringLiteral ParagraphIdType = "Type";
    inline constexpr StringLiteral ParagraphIdVersion = "Version";
    inline constexpr StringLiteral ParagraphIdStatus = "Status";

    // Switches are lowercase separated by dashes
    inline constexpr StringLiteral SwitchAbiToolsUseExactVersions = "abi-tools-use-exact-versions";
    inline constexpr StringLiteral SwitchAddInitialBaseline = "add-initial-baseline";
    inline constexpr StringLiteral SwitchAll = "all";
    inline constexpr StringLiteral SwitchAllLanguages = "all-languages";
    inline constexpr StringLiteral SwitchAllowUnexpectedPassing = "allow-unexpected-passing";
    inline constexpr StringLiteral SwitchAllowUnsupported = "allow-unsupported";
    inline constexpr StringLiteral SwitchApplication = "application";
    inline constexpr StringLiteral SwitchArm = "arm";
    inline constexpr StringLiteral SwitchArm64 = "arm64";
    inline constexpr StringLiteral SwitchAssetSources = "asset-sources";
    inline constexpr StringLiteral SwitchBaseline = "baseline";
    inline constexpr StringLiteral SwitchBin = "bin";
    inline constexpr StringLiteral SwitchBinarycaching = "binarycaching";
    inline constexpr StringLiteral SwitchBinarysource = "binarysource";
    inline constexpr StringLiteral SwitchBuildtrees = "buildtrees";
    inline constexpr StringLiteral SwitchBuildtreesRoot = "buildtrees-root";
    inline constexpr StringLiteral SwitchBuiltinPortsRoot = "builtin-ports-root";
    inline constexpr StringLiteral SwitchBuiltinRegistryVersionsDir = "builtin-registry-versions-dir";
    inline constexpr StringLiteral SwitchCIBaseline = "ci-baseline";
    inline constexpr StringLiteral SwitchCleanAfterBuild = "clean-after-build";
    inline constexpr StringLiteral SwitchCleanBuildtreesAfterBuild = "clean-buildtrees-after-build";
    inline constexpr StringLiteral SwitchCleanDownloadsAfterBuild = "clean-downloads-after-build";
    inline constexpr StringLiteral SwitchCleanPackagesAfterBuild = "clean-packages-after-build";
    inline constexpr StringLiteral SwitchCMakeArgs = "cmake-args";
    inline constexpr StringLiteral SwitchCMakeConfigureDebug = "cmake-configure-debug";
    inline constexpr StringLiteral SwitchCMakeDebug = "cmake-debug";
    inline constexpr StringLiteral SwitchConvertControl = "convert-control";
    inline constexpr StringLiteral SwitchCopiedFilesLog = "copied-files-log";
    inline constexpr StringLiteral SwitchDebug = "debug";
    inline constexpr StringLiteral SwitchDebugBin = "debug-bin";
    inline constexpr StringLiteral SwitchDebugEnv = "debug-env";
    inline constexpr StringLiteral SwitchDgml = "dgml";
    inline constexpr StringLiteral SwitchDisableMetrics = "disable-metrics";
    inline constexpr StringLiteral SwitchDot = "dot";
    inline constexpr StringLiteral SwitchDownloadsRoot = "downloads-root";
    inline constexpr StringLiteral SwitchDryRun = "dry-run";
    inline constexpr StringLiteral SwitchEditable = "editable";
    inline constexpr StringLiteral SwitchEnforcePortChecks = "enforce-port-checks";
    inline constexpr StringLiteral SwitchExclude = "exclude";
    inline constexpr StringLiteral SwitchFailureLogs = "failure-logs";
    inline constexpr StringLiteral SwitchFeatureFlags = "feature-flags";
    inline constexpr StringLiteral SwitchForce = "force";
    inline constexpr StringLiteral SwitchFormat = "format";
    inline constexpr StringLiteral SwitchFreeBsd = "freebsd";
    inline constexpr StringLiteral SwitchHead = "head";
    inline constexpr StringLiteral SwitchHeader = "header";
    inline constexpr StringLiteral SwitchHostExclude = "host-exclude";
    inline constexpr StringLiteral SwitchHostTriplet = "host-triplet";
    inline constexpr StringLiteral SwitchIfw = "ifw";
    inline constexpr StringLiteral SwitchIfwConfigFilePath = "ifw-configuration-file-path";
    inline constexpr StringLiteral SwitchIfwInstallerFilePath = "ifw-installer-file-path";
    inline constexpr StringLiteral SwitchIfwPackagesDirPath = "ifw-packages-directory-path";
    inline constexpr StringLiteral SwitchIfwRepositoryUrl = "ifw-repository-url";
    inline constexpr StringLiteral SwitchIfwRepostitoryDirPath = "ifw-repository-directory-path";
    inline constexpr StringLiteral SwitchIgnoreLockFailures = "ignore-lock-failures";
    inline constexpr StringLiteral SwitchInclude = "include";
    inline constexpr StringLiteral SwitchInstalledBinDir = "installed-bin-dir";
    inline constexpr StringLiteral SwitchInstallRoot = "install-root";
    inline constexpr StringLiteral SwitchJson = "json";
    inline constexpr StringLiteral SwitchKeepGoing = "keep-going";
    inline constexpr StringLiteral SwitchLinux = "linux";
    inline constexpr StringLiteral SwitchManifestRoot = "manifest-root";
    inline constexpr StringLiteral SwitchMaxRecurse = "max-recurse";
    inline constexpr StringLiteral SwitchMSBuildProps = "msbuild-props";
    inline constexpr StringLiteral SwitchName = "name";
    inline constexpr StringLiteral SwitchNoDownloads = "no-downloads";
    inline constexpr StringLiteral SwitchNoDryRun = "no-dry-run";
    inline constexpr StringLiteral SwitchNoKeepGoing = "no-keep-going";
    inline constexpr StringLiteral SwitchNoOutputComments = "no-output-comments";
    inline constexpr StringLiteral SwitchNoPrintUsage = "no-print-usage";
    inline constexpr StringLiteral SwitchNormalize = "normalize";
    inline constexpr StringLiteral SwitchNuGet = "nuget";
    inline constexpr StringLiteral SwitchNuGetDescription = "nuget-description";
    inline constexpr StringLiteral SwitchNuGetId = "nuget-id";
    inline constexpr StringLiteral SwitchNuGetVersion = "nuget-version";
    inline constexpr StringLiteral SwitchOnlyBinarycaching = "only-binarycaching";
    inline constexpr StringLiteral SwitchOnlyDownloads = "only-downloads";
    inline constexpr StringLiteral SwitchOsx = "osx";
    inline constexpr StringLiteral SwitchOutdated = "outdated";
    inline constexpr StringLiteral SwitchOutput = "output";
    inline constexpr StringLiteral SwitchOutputDir = "output-dir";
    inline constexpr StringLiteral SwitchOutputHashes = "output-hashes";
    inline constexpr StringLiteral SwitchOverlayPorts = "overlay-ports";
    inline constexpr StringLiteral SwitchOverlayTriplets = "overlay-triplets";
    inline constexpr StringLiteral SwitchOverwriteVersion = "overwrite-version";
    inline constexpr StringLiteral SwitchPackagesRoot = "packages-root";
    inline constexpr StringLiteral SwitchParentHashes = "parent-hashes";
    inline constexpr StringLiteral SwitchPrefab = "prefab";
    inline constexpr StringLiteral SwitchPrefabArtifactId = "prefab-artifact-id";
    inline constexpr StringLiteral SwitchPrefabDebug = "prefab-debug";
    inline constexpr StringLiteral SwitchPrefabGroupId = "prefab-group-id";
    inline constexpr StringLiteral SwitchPrefabMaven = "prefab-maven";
    inline constexpr StringLiteral SwitchPrefabMinSdk = "prefab-min-sdk";
    inline constexpr StringLiteral SwitchPrefabTargetSdk = "prefab-target-sdk";
    inline constexpr StringLiteral SwitchPrefabVersion = "prefab-version";
    inline constexpr StringLiteral SwitchPrintmetrics = "printmetrics";
    inline constexpr StringLiteral SwitchPurge = "purge";
    inline constexpr StringLiteral SwitchPython = "python";
    inline constexpr StringLiteral SwitchRaw = "raw";
    inline constexpr StringLiteral SwitchRecurse = "recurse";
    inline constexpr StringLiteral SwitchRegistriesCache = "registries-cache";
    inline constexpr StringLiteral SwitchScriptsRoot = "scripts-root";
    inline constexpr StringLiteral SwitchSendmetrics = "sendmetrics";
    inline constexpr StringLiteral SwitchSevenZip = "7zip";
    inline constexpr StringLiteral SwitchSha512 = "sha512";
    inline constexpr StringLiteral SwitchShowDepth = "show-depth";
    inline constexpr StringLiteral SwitchSingleFile = "single-file";
    inline constexpr StringLiteral SwitchSkipFailures = "skip-failures";
    inline constexpr StringLiteral SwitchSkipFormattingCheck = "skip-formatting-check";
    inline constexpr StringLiteral SwitchSkipSha512 = "skip-sha512";
    inline constexpr StringLiteral SwitchSkipVersionFormatCheck = "skip-version-format-check";
    inline constexpr StringLiteral SwitchSort = "sort";
    inline constexpr StringLiteral SwitchStore = "store";
    inline constexpr StringLiteral SwitchStrip = "strip";
    inline constexpr StringLiteral SwitchTargetArm = "target:arm";
    inline constexpr StringLiteral SwitchTargetArm64 = "target:arm64";
    inline constexpr StringLiteral SwitchTargetBinary = "target-binary";
    inline constexpr StringLiteral SwitchTargetX64 = "target:x64";
    inline constexpr StringLiteral SwitchTargetX86 = "target:x86";
    inline constexpr StringLiteral SwitchTLogFile = "tlog-file";
    inline constexpr StringLiteral SwitchTools = "tools";
    inline constexpr StringLiteral SwitchTriplet = "triplet";
    inline constexpr StringLiteral SwitchUrl = "url";
    inline constexpr StringLiteral SwitchVcpkgRoot = "vcpkg-root";
    inline constexpr StringLiteral SwitchVerbose = "verbose";
    inline constexpr StringLiteral SwitchVerifyGitTrees = "verify-git-trees";
    inline constexpr StringLiteral SwitchVersion = "version";
    inline constexpr StringLiteral SwitchVersionDate = "version-date";
    inline constexpr StringLiteral SwitchVersionRelaxed = "version-relaxed";
    inline constexpr StringLiteral SwitchVersionString = "version-string";
    inline constexpr StringLiteral SwitchWaitForLock = "wait-for-lock";
    inline constexpr StringLiteral SwitchWindows = "windows";
    inline constexpr StringLiteral SwitchX64 = "x64";
    inline constexpr StringLiteral SwitchX86 = "x86";
    inline constexpr StringLiteral SwitchXAllInstalled = "x-all-installed";
    inline constexpr StringLiteral SwitchXChocolatey = "x-chocolatey";
    inline constexpr StringLiteral SwitchXFeature = "x-feature";
    inline constexpr StringLiteral SwitchXFullDesc = "x-full-desc";
    inline constexpr StringLiteral SwitchXInstalled = "x-installed";
    inline constexpr StringLiteral SwitchXJson = "x-json";
    inline constexpr StringLiteral SwitchXMaintainer = "x-maintainer";
    inline constexpr StringLiteral SwitchXNoDefaultFeatures = "x-no-default-features";
    inline constexpr StringLiteral SwitchXProhibitBackcompatFeatures = "x-prohibit-backcompat-features";
    inline constexpr StringLiteral SwitchXRandomize = "x-randomize";
    inline constexpr StringLiteral SwitchXTransitive = "x-transitive";
    inline constexpr StringLiteral SwitchXUseAria2 = "x-use-aria2";
    inline constexpr StringLiteral SwitchXVersionSuffix = "x-version-suffix";
    inline constexpr StringLiteral SwitchXWriteNuGetPackagesConfig = "x-write-nuget-packages-config";
    inline constexpr StringLiteral SwitchXXUnit = "x-xunit";
    inline constexpr StringLiteral SwitchXXUnitAll = "x-xunit-all";
    inline constexpr StringLiteral SwitchZip = "zip";
    inline constexpr StringLiteral SwitchZMachineReadableProgress = "z-machine-readable-progress";

    // Sorts
    inline constexpr StringLiteral SortLexicographical = "lexicographical";
    inline constexpr StringLiteral SortReverse = "reverse";
    inline constexpr StringLiteral SortTopological = "topological";
    inline constexpr StringLiteral SortXTree = "x-tree";

    // File names
    inline constexpr StringLiteral FileBaselineDotJson = "baseline.json";
    inline constexpr StringLiteral FileBin = "bin";
    inline constexpr StringLiteral FileControl = "CONTROL";
    inline constexpr StringLiteral FileDebug = "debug";
    inline constexpr StringLiteral FileDetectCompiler = "detect_compiler";
    inline constexpr StringLiteral FileDotDsStore = ".DS_Store";
    inline constexpr StringLiteral FileInclude = "include";
    inline constexpr StringLiteral FilePortfileDotCMake = "portfile.cmake";
    inline constexpr StringLiteral FileTools = "tools";
    inline constexpr StringLiteral FileVcpkgAbiInfo = "vcpkg_abi_info.txt";
    inline constexpr StringLiteral FileVcpkgBundleDotJson = "vcpkg-bundle.json";
    inline constexpr StringLiteral FileVcpkgConfigurationDotJson = "vcpkg-configuration.json";
    inline constexpr StringLiteral FileVcpkgDotJson = "vcpkg.json";
    inline constexpr StringLiteral FileVcpkgPathTxt = "vcpkg.path.txt";
    inline constexpr StringLiteral FileVcpkgUserProps = "vcpkg.user.props";
    inline constexpr StringLiteral FileVcpkgUserTargets = "vcpkg.user.targets";
    inline constexpr StringLiteral FileVersions = "versions";

    // CMake variables are usually ALL_CAPS_WITH_UNDERSCORES
    inline constexpr StringLiteral CMakeVariableAllFeatures = "ALL_FEATURES";
    inline constexpr StringLiteral CMakeVariableBaseVersion = "VCPKG_BASE_VERSION";
    inline constexpr StringLiteral CMakeVariableBuildType = "VCPKG_BUILD_TYPE";
    inline constexpr StringLiteral CMakeVariableChainloadToolchainFile = "VCPKG_CHAINLOAD_TOOLCHAIN_FILE";
    inline constexpr StringLiteral CMakeVariableCMakeSystemName = "VCPKG_CMAKE_SYSTEM_NAME";
    inline constexpr StringLiteral CMakeVariableCMakeSystemVersion = "VCPKG_CMAKE_SYSTEM_VERSION";
    inline constexpr StringLiteral CMakeVariableCmd = "CMD";
    inline constexpr StringLiteral CMakeVariableConcurrency = "VCPKG_CONCURRENCY";
    inline constexpr StringLiteral CMakeVariableCurrentBuildtreesDir = "CURRENT_BUILDTREES_DIR";
    inline constexpr StringLiteral CMakeVariableCurrentPackagesDir = "CURRENT_PACKAGES_DIR";
    inline constexpr StringLiteral CMakeVariableCurrentPortDir = "CURRENT_PORT_DIR";
    inline constexpr StringLiteral CMakeVariableDisableCompilerTracking = "VCPKG_DISABLE_COMPILER_TRACKING";
    inline constexpr StringLiteral CMakeVariableDownloadMode = "VCPKG_DOWNLOAD_MODE";
    inline constexpr StringLiteral CMakeVariableDownloads = "DOWNLOADS";
    inline constexpr StringLiteral CMakeVariableDownloadTool = "_VCPKG_DOWNLOAD_TOOL";
    inline constexpr StringLiteral CMakeVariableEditable = "_VCPKG_EDITABLE";
    inline constexpr StringLiteral CMakeVariableEnvPassthrough = "VCPKG_ENV_PASSTHROUGH";
    inline constexpr StringLiteral CMakeVariableEnvPassthroughUntracked = "VCPKG_ENV_PASSTHROUGH_UNTRACKED";
    inline constexpr StringLiteral CMakeVariableFeatures = "FEATURES";
    inline constexpr StringLiteral CMakeVariableFilename = "FILENAME";
    inline constexpr StringLiteral CMakeVariableGit = "GIT";
    inline constexpr StringLiteral CMakeVariableHostTriplet = "_HOST_TRIPLET";
    inline constexpr StringLiteral CMakeVariableLoadVcvarsEnv = "VCPKG_LOAD_VCVARS_ENV";
    inline constexpr StringLiteral CMakeVariableNoDownloads = "_VCPKG_NO_DOWNLOADS";
    inline constexpr StringLiteral CMakeVariablePlatformToolset = "VCPKG_PLATFORM_TOOLSET";
    inline constexpr StringLiteral CMakeVariablePlatformToolsetVersion = "VCPKG_PLATFORM_TOOLSET_VERSION";
    inline constexpr StringLiteral CMakeVariablePolicyAllowObsoleteMsvcrt = "VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT";
    inline constexpr StringLiteral CMakeVariablePolicyAllowRestrictedHeaders = "VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS";
    inline constexpr StringLiteral CMakeVariablePolicyCMakeHelperPort = "VCPKG_POLICY_CMAKE_HELPER_PORT";
    inline constexpr StringLiteral CMakeVariablePolicyDllsInStaticLibrary = "VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY";
    inline constexpr StringLiteral CMakeVariablePolicyDllsWithoutExports = "VCPKG_POLICY_DLLS_WITHOUT_EXPORTS";
    inline constexpr StringLiteral CMakeVariablePolicyDllsWithoutLibs = "VCPKG_POLICY_DLLS_WITHOUT_LIBS";
    inline constexpr StringLiteral CMakeVariablePolicyEmptyIncludeFolder = "VCPKG_POLICY_EMPTY_INCLUDE_FOLDER";
    inline constexpr StringLiteral CMakeVariablePolicyEmptyPackage = "VCPKG_POLICY_EMPTY_PACKAGE";
    inline constexpr StringLiteral CMakeVariablePolicyMismatchedNumberOfBinaries =
        "VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES";
    inline constexpr StringLiteral CMakeVariablePolicyOnlyReleaseCrt = "VCPKG_POLICY_ONLY_RELEASE_CRT";
    inline constexpr StringLiteral CMakeVariablePolicySkipAbsolutePathsCheck = "VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK";
    inline constexpr StringLiteral CMakeVariablePolicySkipArchitectureCheck = "VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK";
    inline constexpr StringLiteral CMakeVariablePolicySkipDumpbinChecks = "VCPKG_POLICY_SKIP_DUMPBIN_CHECKS";
    inline constexpr StringLiteral CMakeVariablePort = "PORT";
    inline constexpr StringLiteral CMakeVariablePortConfigs = "VCPKG_PORT_CONFIGS";
    inline constexpr StringLiteral CMakeVariableProhibitBackcompatFeatures = "_VCPKG_PROHIBIT_BACKCOMPAT_FEATURES";
    inline constexpr StringLiteral CMakeVariablePublicAbiOverride = "VCPKG_PUBLIC_ABI_OVERRIDE";
    inline constexpr StringLiteral CMakeVariableRef = "REF";
    inline constexpr StringLiteral CMakeVariableRepo = "REPO";
    inline constexpr StringLiteral CMakeVariableSHA512 = "SHA512";
    inline constexpr StringLiteral CMakeVariableTargetArchitecture = "VCPKG_TARGET_ARCHITECTURE";
    inline constexpr StringLiteral CMakeVariableTargetTriplet = "TARGET_TRIPLET";
    inline constexpr StringLiteral CMakeVariableTargetTripletFile = "TARGET_TRIPLET_FILE";
    inline constexpr StringLiteral CMakeVariableUrl = "URL";
    inline constexpr StringLiteral CMakeVariableUrls = "URLS";
    inline constexpr StringLiteral CMakeVariableUseHeadVersion = "VCPKG_USE_HEAD_VERSION";
    inline constexpr StringLiteral CMakeVariableVersion = "VERSION";
    inline constexpr StringLiteral CMakeVariableVisualStudioPath = "VCPKG_VISUAL_STUDIO_PATH";
    inline constexpr StringLiteral CMakeVariableXBoxConsoleTarget = "VCPKG_XBOX_CONSOLE_TARGET";
    inline constexpr StringLiteral CMakeVariableZChainloadToolchainFile = "Z_VCPKG_CHAINLOAD_TOOLCHAIN_FILE";
    inline constexpr StringLiteral CMakeVariableZVcpkgGameDKLatest = "Z_VCPKG_GameDKLatest";

    // Policies are PascalCase
    inline constexpr StringLiteral PolicyAllowObsoleteMsvcrt = "PolicyAllowObsoleteMsvcrt";
    inline constexpr StringLiteral PolicyAllowRestrictedHeaders = "PolicyAllowRestrictedHeaders";
    inline constexpr StringLiteral PolicyCMakeHelperPort = "PolicyCmakeHelperPort";
    inline constexpr StringLiteral PolicyDllsInStaticLibrary = "PolicyDLLsInStaticLibrary";
    inline constexpr StringLiteral PolicyDllsWithoutExports = "PolicyDLLsWithoutExports";
    inline constexpr StringLiteral PolicyDllsWithoutLibs = "PolicyDLLsWithoutLIBs";
    inline constexpr StringLiteral PolicyEmptyIncludeFolder = "PolicyEmptyIncludeFolder";
    inline constexpr StringLiteral PolicyEmptyPackage = "PolicyEmptyPackage";
    inline constexpr StringLiteral PolicyMismatchedNumberOfBinaries = "PolicyMismatchedNumberOfBinaries";
    inline constexpr StringLiteral PolicyOnlyReleaseCrt = "PolicyOnlyReleaseCRT";
    inline constexpr StringLiteral PolicySkipAbsolutePathsCheck = "PolicySkipAbsolutePathsCheck";
    inline constexpr StringLiteral PolicySkipArchitectureCheck = "PolicySkipArchitectureCheck";
    inline constexpr StringLiteral PolicySkipDumpbinChecks = "PolicySkipDumpbinChecks";

    // Environment variables are ALL_CAPS_WITH_UNDERSCORES
    inline constexpr StringLiteral EnvironmentVariableActionsCacheUrl = "ACTIONS_CACHE_URL";
    inline constexpr StringLiteral EnvironmentVariableActionsRuntimeToken = "ACTIONS_RUNTIME_TOKEN";
    inline constexpr StringLiteral EnvironmentVariableAndroidNdkHome = "ANDROID_NDK_HOME";
    inline constexpr StringLiteral EnvironmentVariableAppData = "APPDATA";
    inline constexpr StringLiteral EnvironmentVariableAppveyor = "APPVEYOR";
    inline constexpr StringLiteral EnvironmentVariableBuildId = "BUILD_ID";
    inline constexpr StringLiteral EnvironmentVariableBuildNumber = "BUILD_NUMBER";
    inline constexpr StringLiteral EnvironmentVariableBuildRepositoryId = "BUILD_REPOSITORY_ID";
    inline constexpr StringLiteral EnvironmentVariableCI = "CI";
    inline constexpr StringLiteral EnvironmentVariableCIProjectId = "CI_PROJECT_ID";
    inline constexpr StringLiteral EnvironmentVariableCircleCI = "CIRCLECI";
    inline constexpr StringLiteral EnvironmentVariableCodebuildBuildId = "CODEBUILD_BUILD_ID";
    inline constexpr StringLiteral EnvironmentVariableEditor = "EDITOR";
    inline constexpr StringLiteral EnvironmentVariableGitCeilingDirectories = "GIT_CEILING_DIRECTORIES";
    inline constexpr StringLiteral EnvironmentVariableGitHubActions = "GITHUB_ACTIONS";
    inline constexpr StringLiteral EnvironmentVariableGitHubJob = "GITHUB_JOB";
    inline constexpr StringLiteral EnvironmentVariableGitHubRef = "GITHUB_REF";
    inline constexpr StringLiteral EnvironmentVariableGitHubRepository = "GITHUB_REPOSITORY";
    inline constexpr StringLiteral EnvironmentVariableGitHubRepositoryID = "GITHUB_REPOSITORY_ID";
    inline constexpr StringLiteral EnvironmentVariableGitHubRepositoryOwnerId = "GITHUB_REPOSITORY_OWNER_ID";
    inline constexpr StringLiteral EnvironmentVariableGitHubRunId = "GITHUB_RUN_ID";
    inline constexpr StringLiteral EnvironmentVariableGitHubServerUrl = "GITHUB_SERVER_URL";
    inline constexpr StringLiteral EnvironmentVariableGitHubSha = "GITHUB_SHA";
    inline constexpr StringLiteral EnvironmentVariableGitHubToken = "GITHUB_TOKEN";
    inline constexpr StringLiteral EnvironmentVariableGitHubWorkflow = "GITHUB_WORKFLOW";
    inline constexpr StringLiteral EnvironmentVariableGitLabCI = "GITLAB_CI";
    inline constexpr StringLiteral EnvironmentVariableHerokuTestRunId = "HEROKU_TEST_RUN_ID";
    inline constexpr StringLiteral EnvironmentVariableHome = "HOME";
    inline constexpr StringLiteral EnvironmentVariableHttpProxy = "HTTP_PROXY";
    inline constexpr StringLiteral EnvironmentVariableHttpsProxy = "HTTPS_PROXY";
    inline constexpr StringLiteral EnvironmentVariableInclude = "INCLUDE";
    inline constexpr StringLiteral EnvironmentVariableJenkinsHome = "JENKINS_HOME";
    inline constexpr StringLiteral EnvironmentVariableJenkinsUrl = "JENKINS_URL";
    inline constexpr StringLiteral EnvironmentVariableLocalAppData = "LOCALAPPDATA";
    inline constexpr StringLiteral EnvironmentVariableOverlayTriplets = "VCPKG_OVERLAY_TRIPLETS";
    inline constexpr StringLiteral EnvironmentVariablePath = "PATH";
    inline constexpr StringLiteral EnvironmentVariablePlatform = "Platform";
    inline constexpr StringLiteral EnvironmentVariableProgramFiles = "PROGRAMFILES";
    inline constexpr StringLiteral EnvironmentVariableProgramFilesX86 = "ProgramFiles(x86)";
    inline constexpr StringLiteral EnvironmentVariableProgramW6432 = "ProgramW6432";
    inline constexpr StringLiteral EnvironmentVariablePythonPath = "PYTHONPATH";
    inline constexpr StringLiteral EnvironmentVariableSystemRoot = "SystemRoot";
    inline constexpr StringLiteral EnvironmentVariableTeamcityVersion = "TEAMCITY_VERSION";
    inline constexpr StringLiteral EnvironmentVariableTfBuild = "TF_BUILD";
    inline constexpr StringLiteral EnvironmentVariableTravis = "TRAVIS";
    inline constexpr StringLiteral EnvironmentVariableUserprofile = "USERPROFILE";
    inline constexpr StringLiteral EnvironmentVariableVCInstallDir = "VCINSTALLDIR";
    inline constexpr StringLiteral EnvironmentVariableVcpkgBinarySources = "VCPKG_BINARY_SOURCES";
    inline constexpr StringLiteral EnvironmentVariableVcpkgCommand = "VCPKG_COMMAND";
    inline constexpr StringLiteral EnvironmentVariableVcpkgDefaultBinaryCache = "VCPKG_DEFAULT_BINARY_CACHE";
    inline constexpr StringLiteral EnvironmentVariableVcpkgDefaultHostTriplet = "VCPKG_DEFAULT_HOST_TRIPLET";
    inline constexpr StringLiteral EnvironmentVariableVcpkgDefaultTriplet = "VCPKG_DEFAULT_TRIPLET";
    inline constexpr StringLiteral EnvironmentVariableVcpkgDisableMetrics = "VCPKG_DISABLE_METRICS";
    inline constexpr StringLiteral EnvironmentVariableVcpkgDownloads = "VCPKG_DOWNLOADS";
    inline constexpr StringLiteral EnvironmentVariableVcpkgFeatureFlags = "VCPKG_FEATURE_FLAGS";
    inline constexpr StringLiteral EnvironmentVariableVcpkgForceDownloadedBinaries = "VCPKG_FORCE_DOWNLOADED_BINARIES";
    inline constexpr StringLiteral EnvironmentVariableVcpkgForceSystemBinaries = "VCPKG_FORCE_SYSTEM_BINARIES";
    inline constexpr StringLiteral EnvironmentVariableVcpkgKeepEnvVars = "VCPKG_KEEP_ENV_VARS";
    inline constexpr StringLiteral EnvironmentVariableVcpkgMaxConcurrency = "VCPKG_MAX_CONCURRENCY";
    inline constexpr StringLiteral EnvironmentVariableVcpkgNoCi = "VCPKG_NO_CI";
    inline constexpr StringLiteral EnvironmentVariableVcpkgNuGetRepository = "VCPKG_NUGET_REPOSITORY";
    inline constexpr StringLiteral EnvironmentVariableVcpkgOverlayPorts = "VCPKG_OVERLAY_PORTS";
    inline constexpr StringLiteral EnvironmentVariableVcpkgRoot = "VCPKG_ROOT";
    inline constexpr StringLiteral EnvironmentVariableVcpkgUseNuGetCache = "VCPKG_USE_NUGET_CACHE";
    inline constexpr StringLiteral EnvironmentVariableVcpkgVisualStudioPath = "VCPKG_VISUAL_STUDIO_PATH";
    inline constexpr StringLiteral EnvironmentVariableVscmdArgTgtArch = "VSCMD_ARG_TGT_ARCH";
    inline constexpr StringLiteral EnvironmentVariableVSCmdSkipSendTelemetry = "VSCMD_SKIP_SENDTELEMETRY";
    inline constexpr StringLiteral EnvironmentVariableVsLang = "VSLANG";
    inline constexpr StringLiteral EnvironmentVariableXVcpkgAssetSources = "X_VCPKG_ASSET_SOURCES";
    inline constexpr StringLiteral EnvironmentVariableXVcpkgIgnoreLockFailures = "X_VCPKG_IGNORE_LOCK_FAILURES";
    inline constexpr StringLiteral EnvironmentVariableXVcpkgNuGetIDPrefix = "X_VCPKG_NUGET_ID_PREFIX";
    inline constexpr StringLiteral EnvironmentVariableXVcpkgRecursiveData = "X_VCPKG_RECURSIVE_DATA";
    inline constexpr StringLiteral EnvironmentVariableXVcpkgRegistriesCache = "X_VCPKG_REGISTRIES_CACHE";

    inline constexpr StringLiteral FeatureNameCore = "core";
    inline constexpr StringLiteral FeatureNameDefault = "default";
    inline constexpr StringLiteral FeatureNameStar = "*";

    inline constexpr StringLiteral FeatureFlagBinarycaching = "binarycaching";
    inline constexpr StringLiteral FeatureFlagCompilertracking = "compilertracking";
    inline constexpr StringLiteral FeatureFlagDependencygraph = "dependencygraph";
    inline constexpr StringLiteral FeatureFlagFeaturepackages = "featurepackages";
    inline constexpr StringLiteral FeatureFlagManifests = "manifests";
    inline constexpr StringLiteral FeatureFlagRegistries = "registries";
    inline constexpr StringLiteral FeatureFlagVersions = "versions";

    inline constexpr StringLiteral MarkerCompilerHash = "#COMPILER_HASH#";
    inline constexpr StringLiteral MarkerCompilerCxxVersion = "#COMPILER_CXX_VERSION#";
    inline constexpr StringLiteral MarkerCompilerCxxId = "#COMPILER_CXX_ID#";

    inline constexpr StringLiteral AbiTagCMake = "cmake";
    inline constexpr StringLiteral AbiTagFeatures = "features";
    inline constexpr StringLiteral AbiTagGrdkH = "grdk.h";
    inline constexpr StringLiteral AbiTagTriplet = "triplet";
    inline constexpr StringLiteral AbiTagTripletAbi = "triplet_abi";
    inline constexpr StringLiteral AbiTagPortsDotCMake = "ports.cmake";
    inline constexpr StringLiteral AbiTagPostBuildChecks = "post_build_checks";
    inline constexpr StringLiteral AbiTagPowershell = "powershell";
    inline constexpr StringLiteral AbiTagPublicAbiOverride = "public_abi_override";
}
