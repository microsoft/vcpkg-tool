#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

StringView vcpkg::extract_first_cmake_invocation_args(StringView content, StringView command)
{
    // command\s*\(([^)]+)\)
    auto it = content.begin();
    do
    {
        it = Util::search_and_skip(it, content.end(), command);
        if (it == content.end())
        {
            return {};
        }

        while (ParserBase::is_whitespace(*it))
        {
            ++it;
            if (it == content.end())
            {
                return {};
            }
        }
        // if we don't get a ( here, then we matched a prefix of the command but not the command itself
    } while (*it != '(');
    ++it;
    auto it_end = std::find(it, content.end(), ')');
    if (it_end == content.end())
    {
        return {};
    }

    return StringView{it, it_end};
}

StringView vcpkg::extract_arg_from_cmake_invocation_args(StringView invocation_args, StringView target_arg)
{
    auto it = invocation_args.begin();
    do
    {
        it = Util::search_and_skip(it, invocation_args.end(), target_arg);
        if (it == invocation_args.end())
        {
            return {};
        }
    } while (!ParserBase::is_whitespace(*it));
    it = std::find_if_not(it, invocation_args.end(), ParserBase::is_whitespace);
    if (it == invocation_args.end())
    {
        return {};
    }

    if (*it == '"')
    {
        // quoted value
        ++it;
        auto it_end = std::find(it, invocation_args.end(), '"');
        if (it_end == invocation_args.end())
        {
            return {};
        }

        return {it, it_end};
    }

    // unquoted value
    return {it, std::find_if(it + 1, invocation_args.end(), ParserBase::is_whitespace)};
}

std::string vcpkg::replace_cmake_var(StringView text, StringView var, StringView value)
{
    std::string replacement;
    replacement.reserve(var.size() + 3);
    replacement.append("${", 2);
    replacement.append(var.data(), var.size());
    replacement.push_back('}');
    return Strings::replace_all(text, replacement, value);
}

static std::string fix_ref_version(StringView ref, StringView version)
{
    return replace_cmake_var(ref, CMakeVariableVersion, version);
}

static void append_move_if_exists_and_array(Json::Array& out, Json::Object& obj, StringView property)
{
    if (auto p = obj.get(property))
    {
        if (auto arr = p->maybe_array())
        {
            for (auto& e : *arr)
            {
                out.push_back(std::move(e));
            }
        }
    }
}

static Json::Object make_resource(
    std::string spdxid, StringView name, std::string downloadLocation, StringView sha512, StringView filename)
{
    Json::Object obj;
    obj.insert(SpdxSpdxId, std::move(spdxid));
    obj.insert(JsonIdName, name);
    if (!filename.empty())
    {
        obj.insert(SpdxPackageFileName, filename);
    }
    obj.insert(SpdxDownloadLocation, std::move(downloadLocation));
    obj.insert(SpdxLicenseConcluded, SpdxNoAssertion);
    obj.insert(SpdxLicenseDeclared, SpdxNoAssertion);
    obj.insert(SpdxCopyrightText, SpdxNoAssertion);
    if (!sha512.empty())
    {
        auto& chk = obj.insert(JsonIdChecksums, Json::Array());
        auto& chk512 = chk.push_back(Json::Object());
        chk512.insert(JsonIdAlgorithm, JsonIdAllCapsSHA512);
        chk512.insert(SpdxChecksumValue, Strings::ascii_to_lowercase(sha512));
    }
    return obj;
}

static void find_all_github(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto github = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_from_github");
        if (github.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_arg_from_cmake_invocation_args(github, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_arg_from_cmake_invocation_args(github, CMakeVariableRef), version_text);
        auto sha = extract_arg_from_cmake_invocation_args(github, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", packages.size()),
                                         repo,
                                         fmt::format("git+https://github.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
        it = github.end();
    }
}

static void find_all_bitbucket(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto bitbucket = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_from_bitbucket");
        if (bitbucket.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_arg_from_cmake_invocation_args(bitbucket, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_arg_from_cmake_invocation_args(bitbucket, CMakeVariableRef), version_text);
        auto sha = extract_arg_from_cmake_invocation_args(bitbucket, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", packages.size()),
                                         repo,
                                         fmt::format("git+https://bitbucket.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
        it = bitbucket.end();
    }
}

static void find_all_gitlab(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto gitlab = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_from_gitlab");
        if (gitlab.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_arg_from_cmake_invocation_args(gitlab, CMakeVariableRepo);
        auto url = extract_arg_from_cmake_invocation_args(gitlab, CMakeVariableGitlabUrl);
        auto ref = fix_ref_version(extract_arg_from_cmake_invocation_args(gitlab, CMakeVariableRef), version_text);
        auto sha = extract_arg_from_cmake_invocation_args(gitlab, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", packages.size()),
                                         repo,
                                         fmt::format("git+{}/{}@{}", url, repo, ref),
                                         sha,
                                         {}));
        it = gitlab.end();
    }
}

static void find_all_git(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto git = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_from_git");
        if (git.empty())
        {
            it = text.end();
            continue;
        }
        auto url = extract_arg_from_cmake_invocation_args(git, CMakeVariableUrl);
        auto ref = fix_ref_version(extract_arg_from_cmake_invocation_args(git, CMakeVariableRef), version_text);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", packages.size()), url, fmt::format("git+{}@{}", url, ref), {}, {}));
        it = git.end();
    }
}

static void find_all_distfile(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto distfile = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_download_distfile");
        if (distfile.empty())
        {
            it = text.end();
            continue;
        }
        auto url = fix_ref_version(extract_arg_from_cmake_invocation_args(distfile, CMakeVariableUrls), version_text);
        auto filename =
            fix_ref_version(extract_arg_from_cmake_invocation_args(distfile, CMakeVariableFilename), version_text);
        auto sha = extract_arg_from_cmake_invocation_args(distfile, CMakeVariableSHA512);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", packages.size()), filename, std::move(url), sha, filename));
        it = distfile.end();
    }
}

static void find_all_sourceforge(StringView text, Json::Array& packages, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto sfg = extract_first_cmake_invocation_args(StringView{it, text.end()}, "vcpkg_from_sourceforge");
        if (sfg.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_arg_from_cmake_invocation_args(sfg, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_arg_from_cmake_invocation_args(sfg, CMakeVariableRef), version_text);
        auto filename =
            fix_ref_version(extract_arg_from_cmake_invocation_args(sfg, CMakeVariableFilename), version_text);
        auto sha = extract_arg_from_cmake_invocation_args(sfg, CMakeVariableSHA512);
        auto url = fmt::format("https://sourceforge.net/projects/{}/files/{}/{}", repo, ref, filename);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", packages.size()), filename, std::move(url), sha, filename));
        it = sfg.end();
    }
}

Json::Object vcpkg::run_resource_heuristics(StringView contents, StringView version_text)
{
    // These are a sequence of heuristics to enable proof-of-concept extraction of remote resources for SPDX SBOM
    // inclusion
    Json::Object ret;
    auto& packages = ret.insert(JsonIdPackages, Json::Array{});

    find_all_github(contents, packages, version_text);
    find_all_gitlab(contents, packages, version_text);
    find_all_git(contents, packages, version_text);
    find_all_distfile(contents, packages, version_text);
    find_all_sourceforge(contents, packages, version_text);
    find_all_bitbucket(contents, packages, version_text);

    return ret;
}

std::string vcpkg::create_spdx_sbom(const InstallPlanAction& action,
                                    View<Path> relative_paths,
                                    View<std::string> hashes,
                                    View<Path> relative_package_paths,
                                    View<std::string> package_hashes,
                                    std::string created_time,
                                    std::string document_namespace,
                                    std::vector<Json::Object>&& resource_docs)
{
    Checks::check_exit(VCPKG_LINE_INFO, relative_paths.size() == hashes.size());
    Checks::check_exit(VCPKG_LINE_INFO, relative_package_paths.size() == package_hashes.size());

    const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
    const auto& cpgh = *scfl.source_control_file->core_paragraph;
    StringView abi{SpdxNone};
    if (auto package_abi = action.package_abi().get())
    {
        abi = *package_abi;
    }

    const auto stringized_license = calculate_spdx_license(action);
    Json::Object doc;
    doc.insert(JsonIdDollarSchema, "https://raw.githubusercontent.com/spdx/spdx-spec/v2.2.1/schemas/spdx-schema.json");
    doc.insert(SpdxVersion, SpdxTwoTwo);
    doc.insert(SpdxDataLicense, SpdxCCZero);
    doc.insert(SpdxSpdxId, SpdxRefDocument);
    doc.insert(SpdxDocumentNamespace, std::move(document_namespace));
    doc.insert(JsonIdName, fmt::format("{}@{} {}", action.spec, cpgh.version, abi));
    {
        auto& cinfo = doc.insert(SpdxCreationInfo, Json::Object());
        auto& creators = cinfo.insert(JsonIdCreators, Json::Array());
        creators.push_back(Strings::concat("Tool: vcpkg-", VCPKG_BASE_VERSION_AS_STRING, '-', VCPKG_VERSION_AS_STRING));
        cinfo.insert(JsonIdCreated, std::move(created_time));
    }

    auto& rels = doc.insert(JsonIdRelationships, Json::Array());
    auto& packages = doc.insert(JsonIdPackages, Json::Array());
    {
        auto& obj = packages.push_back(Json::Object());
        obj.insert(JsonIdName, action.spec.name());
        obj.insert(SpdxSpdxId, SpdxRefPort);
        obj.insert(SpdxVersionInfo, cpgh.version.to_string());
        obj.insert(SpdxDownloadLocation, scfl.spdx_location.empty() ? StringView{SpdxNoAssertion} : scfl.spdx_location);
        if (!cpgh.homepage.empty())
        {
            obj.insert(JsonIdHomepage, cpgh.homepage);
        }
        obj.insert(SpdxLicenseConcluded, stringized_license);
        obj.insert(SpdxLicenseDeclared, SpdxNoAssertion);
        obj.insert(SpdxCopyrightText, SpdxNoAssertion);
        if (!cpgh.summary.empty()) obj.insert(JsonIdSummary, Strings::join("\n", cpgh.summary));
        if (!cpgh.description.empty()) obj.insert(JsonIdDescription, Strings::join("\n", cpgh.description));
        obj.insert(JsonIdComment, "This is the port (recipe) consumed by vcpkg.");
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert(SpdxElementId, SpdxRefPort);
            rel.insert(SpdxRelationshipType, SpdxGenerates);
            rel.insert(SpdxRelatedSpdxElement, SpdxRefBinary);
        }
    }
    {
        auto& obj = packages.push_back(Json::Object());
        obj.insert(JsonIdName, action.spec.to_string());
        obj.insert(SpdxSpdxId, SpdxRefBinary);
        obj.insert(SpdxVersionInfo, abi);
        obj.insert(SpdxDownloadLocation, SpdxNone);
        obj.insert(SpdxLicenseConcluded, stringized_license);
        obj.insert(SpdxLicenseDeclared, SpdxNoAssertion);
        obj.insert(SpdxCopyrightText, SpdxNoAssertion);
        obj.insert(JsonIdComment, "This is a binary package built by vcpkg.");
    }

    auto& files = doc.insert(JsonIdFiles, Json::Array());

    // Helper function to process files and create relationships
    auto process_files =
        [&files,
         &rels](View<Path> paths, View<std::string> file_hashes, StringLiteral ref_name, StringLiteral ref_type) {
            for (size_t i = 0; i < paths.size(); ++i)
            {
                const auto& path = paths[i];
                const auto& hash = file_hashes[i];

                auto& obj = files.push_back(Json::Object());
                obj.insert(SpdxFileName, "./" + path.generic_u8string());
                const auto ref = fmt::format("SPDXRef-{}-file-{}", ref_name, i);
                obj.insert(SpdxSpdxId, ref);
                auto& checksum = obj.insert(JsonIdChecksums, Json::Array());
                auto& checksum1 = checksum.push_back(Json::Object());
                checksum1.insert(JsonIdAlgorithm, JsonIdAllCapsSHA256);
                checksum1.insert(SpdxChecksumValue, hash);
                obj.insert(SpdxLicenseConcluded, SpdxNoAssertion);
                obj.insert(SpdxCopyrightText, SpdxNoAssertion);
                {
                    auto& rel = rels.push_back(Json::Object());
                    rel.insert(SpdxElementId, ref_type);
                    rel.insert(SpdxRelationshipType, SpdxContains);
                    rel.insert(SpdxRelatedSpdxElement, ref);
                }
                if (path == FileVcpkgDotJson && ref_type == SpdxRefPort)
                {
                    auto& rel = rels.push_back(Json::Object());
                    rel.insert(SpdxElementId, ref);
                    rel.insert(SpdxRelationshipType, SpdxDependencyManifestOf);
                    rel.insert(SpdxRelatedSpdxElement, ref_type);
                }
            }
        };

    // Process port files first, then package files
    process_files(relative_paths, hashes, "port", SpdxRefPort);
    process_files(relative_package_paths, package_hashes, "binary", SpdxRefBinary);

    for (auto&& rdoc : resource_docs)
    {
        append_move_if_exists_and_array(rels, rdoc, JsonIdRelationships);
        append_move_if_exists_and_array(files, rdoc, JsonIdFiles);
        append_move_if_exists_and_array(packages, rdoc, JsonIdPackages);
    }

    return Json::stringify(doc);
}

std::string vcpkg::calculate_spdx_license(const InstallPlanAction& action)
{
    const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
    const auto& scf = *scfl.source_control_file;
    const auto& cpgh = *scf.core_paragraph;
    bool all_licenses_undeclared = cpgh.license.kind() == SpdxLicenseDeclarationKind::NotPresent;
    bool any_explicitly_null_licenses = cpgh.license.kind() == SpdxLicenseDeclarationKind::Null;
    std::vector<SpdxApplicableLicenseExpression> licenses = cpgh.license.applicable_licenses();
    for (const auto& feature_name : action.feature_list)
    {
        if (feature_name == FeatureNameCore)
        {
            continue;
        }

        const auto& feature = scf.find_feature(feature_name).value_or_exit(VCPKG_LINE_INFO);
        const auto& feature_appplicable_licenses = feature.license.applicable_licenses();
        all_licenses_undeclared &= feature.license.kind() == SpdxLicenseDeclarationKind::NotPresent;
        any_explicitly_null_licenses |= feature.license.kind() == SpdxLicenseDeclarationKind::Null;
        licenses.insert(licenses.end(), feature_appplicable_licenses.begin(), feature_appplicable_licenses.end());
    }

    std::string stringized_license;
    if (all_licenses_undeclared)
    {
        stringized_license.assign(SpdxNoAssertion.data(), SpdxNoAssertion.size());
    }
    else
    {
        if (any_explicitly_null_licenses)
        {
            licenses.push_back({SpdxLicenseRefVcpkgNull.to_string(), false});
        }

        Util::sort_unique_erase(licenses);
        if (!licenses.empty())
        {
            auto first = licenses.begin();
            const auto last = licenses.end();
            first->to_string(stringized_license);
            while (++first != last)
            {
                stringized_license.append(" AND ");
                first->to_string(stringized_license);
            }
        }
    }

    return stringized_license;
}

Optional<std::string> vcpkg::read_spdx_license_text(StringView text, StringView origin)
{
    // JsonIdPackages[0]/SpdxLicenseConcluded
    auto maybe_parsed = Json::parse_object(text, origin);
    auto parsed = maybe_parsed.get();
    if (!parsed)
    {
        return nullopt;
    }

    auto maybe_packages_value = parsed->get(JsonIdPackages);
    if (!maybe_packages_value)
    {
        return nullopt;
    }

    auto maybe_packages_array = maybe_packages_value->maybe_array();
    if (!maybe_packages_array || maybe_packages_array->size() == 0)
    {
        return nullopt;
    }

    auto maybe_first_package_object = maybe_packages_array->operator[](0).maybe_object();
    if (!maybe_first_package_object)
    {
        return nullopt;
    }

    auto maybe_license_concluded_value = maybe_first_package_object->get(SpdxLicenseConcluded);
    if (!maybe_license_concluded_value)
    {
        return nullopt;
    }

    auto maybe_license_concluded = maybe_license_concluded_value->maybe_string();
    if (!maybe_license_concluded)
    {
        return nullopt;
    }

    if (maybe_license_concluded->empty() || *maybe_license_concluded == SpdxNoAssertion)
    {
        return nullopt;
    }

    return std::move(*maybe_license_concluded);
}
