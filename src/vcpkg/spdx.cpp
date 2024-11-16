#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

StringView vcpkg::find_cmake_invocation(StringView content, StringView command)
{
    auto it = Util::search_and_skip(content.begin(), content.end(), command);
    if (it == content.end() || ParserBase::is_word_char(*it)) return {};
    ++it;
    auto it_end = std::find(it, content.end(), ')');
    if (it_end == content.end()) return {};
    return StringView{it, it_end};
}
StringView vcpkg::extract_cmake_invocation_argument(StringView command, StringView argument)
{
    auto it = Util::search_and_skip(command.begin(), command.end(), argument);
    if (it == command.end() || ParserBase::is_alphanum(*it)) return {};
    it = std::find_if_not(it, command.end(), ParserBase::is_whitespace);
    if (it == command.end()) return {};
    if (*it == '"')
    {
        return {it + 1, std::find(it + 1, command.end(), '"')};
    }
    return {it,
            std::find_if(it + 1, command.end(), [](char ch) { return ParserBase::is_whitespace(ch) || ch == ')'; })};
}
std::string vcpkg::replace_cmake_var(StringView text, StringView var, StringView value)
{
    return Strings::replace_all(text, std::string("${") + var + "}", value);
}

static std::string fix_ref_version(StringView ref, StringView version)
{
    return replace_cmake_var(ref, CMakeVariableVersion, version);
}

static std::string conclude_license(const std::string& license)
{
    if (license.empty()) return SpdxNoAssertion.to_string();
    return license;
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
    obj.insert(JsonIdName, name.to_string());
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

static void find_all_github(StringView text, Json::Array& packages, size_t& n, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto github = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_from_github");
        if (github.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_cmake_invocation_argument(github, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(github, CMakeVariableRef), version_text);
        auto sha = extract_cmake_invocation_argument(github, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", n++),
                                         repo.to_string(),
                                         fmt::format("git+https://github.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
        it = github.end();
    }
}

static void find_all_bitbucket(StringView text, Json::Array& packages, size_t& n, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto bitbucket = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_from_bitbucket");
        if (bitbucket.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_cmake_invocation_argument(bitbucket, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(bitbucket, CMakeVariableRef), version_text);
        auto sha = extract_cmake_invocation_argument(bitbucket, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", n++),
                                         repo.to_string(),
                                         fmt::format("git+https://bitbucket.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
        it = bitbucket.end();
    }
}

static void find_all_gitlab(StringView text, Json::Array& packages, size_t& n, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto gitlab = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_from_gitlab");
        if (gitlab.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_cmake_invocation_argument(gitlab, CMakeVariableRepo);
        auto url = extract_cmake_invocation_argument(gitlab, CMakeVariableGitlabUrl);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(gitlab, CMakeVariableRef), version_text);
        auto sha = extract_cmake_invocation_argument(gitlab, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", n++),
                                         repo.to_string(),
                                         fmt::format("git+{}/{}@{}", url, repo, ref),
                                         sha,
                                         {}));
        it = gitlab.end();
    }
}

static void find_all_git(StringView text, Json::Array& packages, size_t& n, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto git = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_from_git");
        if (git.empty())
        {
            it = text.end();
            continue;
        }
        auto url = extract_cmake_invocation_argument(git, CMakeVariableUrl);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(git, CMakeVariableRef), version_text);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", n++), url.to_string(), fmt::format("git+{}@{}", url, ref), {}, {}));
        it = git.end();
    }
}

static void find_all_distfile(StringView text, Json::Array& packages, size_t& n)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto distfile = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_download_distfile");
        if (distfile.empty())
        {
            it = text.end();
            continue;
        }
        auto url = extract_cmake_invocation_argument(distfile, CMakeVariableUrls);
        auto filename = extract_cmake_invocation_argument(distfile, CMakeVariableFilename);
        auto sha = extract_cmake_invocation_argument(distfile, CMakeVariableSHA512);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", n++), filename.to_string(), url.to_string(), sha, filename));
        it = distfile.end();
    }
}

static void find_all_sourceforge(StringView text, Json::Array& packages, size_t& n, StringView version_text)
{
    auto it = text.begin();
    while (it != text.end())
    {
        auto sfg = find_cmake_invocation(StringView{it, text.end()}, "vcpkg_from_sourceforge");
        if (sfg.empty())
        {
            it = text.end();
            continue;
        }
        auto repo = extract_cmake_invocation_argument(sfg, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(sfg, CMakeVariableRef), version_text);
        auto filename = extract_cmake_invocation_argument(sfg, CMakeVariableFilename);
        auto sha = extract_cmake_invocation_argument(sfg, CMakeVariableSHA512);
        auto url = fmt::format("https://sourceforge.net/projects/{}/files/{}/{}", repo, ref, filename);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", n++), filename.to_string(), std::move(url), sha, filename));
        it = sfg.end();
    }
}

Json::Object vcpkg::run_resource_heuristics(StringView contents, StringView version_text)
{
    // These are a sequence of heuristics to enable proof-of-concept extraction of remote resources for SPDX SBOM
    // inclusion
    size_t n = 0;
    Json::Object ret;
    auto& packages = ret.insert(JsonIdPackages, Json::Array{});

    find_all_github(contents, packages, n, version_text);
    find_all_gitlab(contents, packages, n, version_text);
    find_all_git(contents, packages, n, version_text);
    find_all_distfile(contents, packages, n);
    find_all_sourceforge(contents, packages, n, version_text);
    find_all_bitbucket(contents, packages, n, version_text);

    return ret;
}

std::string vcpkg::create_spdx_sbom(const InstallPlanAction& action,
                                    View<Path> relative_paths,
                                    View<std::string> hashes,
                                    std::string created_time,
                                    std::string document_namespace,
                                    std::vector<Json::Object>&& resource_docs)
{
    Checks::check_exit(VCPKG_LINE_INFO, relative_paths.size() == hashes.size());

    const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
    const auto& cpgh = *scfl.source_control_file->core_paragraph;
    StringView abi{SpdxNone};
    if (auto package_abi = action.package_abi().get())
    {
        abi = *package_abi;
    }

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
        obj.insert(SpdxLicenseConcluded, conclude_license(cpgh.license.value_or("")));
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
        for (size_t i = 0; i < relative_paths.size(); ++i)
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert(SpdxElementId, SpdxRefPort);
            rel.insert(SpdxRelationshipType, SpdxContains);
            rel.insert(SpdxRelatedSpdxElement, fmt::format("SPDXRef-file-{}", i));
        }
    }
    {
        auto& obj = packages.push_back(Json::Object());
        obj.insert(JsonIdName, action.spec.to_string());
        obj.insert(SpdxSpdxId, SpdxRefBinary);
        obj.insert(SpdxVersionInfo, abi);
        obj.insert(SpdxDownloadLocation, SpdxNone);
        obj.insert(SpdxLicenseConcluded, conclude_license(cpgh.license.value_or("")));
        obj.insert(SpdxLicenseDeclared, SpdxNoAssertion);
        obj.insert(SpdxCopyrightText, SpdxNoAssertion);
        obj.insert(JsonIdComment, "This is a binary package built by vcpkg.");
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert(SpdxElementId, SpdxRefBinary);
            rel.insert(SpdxRelationshipType, SpdxGeneratedFrom);
            rel.insert(SpdxRelatedSpdxElement, SpdxRefPort);
        }
    }

    auto& files = doc.insert(JsonIdFiles, Json::Array());
    {
        for (size_t i = 0; i < relative_paths.size(); ++i)
        {
            const auto& path = relative_paths[i];
            const auto& hash = hashes[i];

            auto& obj = files.push_back(Json::Object());
            obj.insert(SpdxFileName, "./" + path.generic_u8string());
            const auto ref = fmt::format("SPDXRef-file-{}", i);
            obj.insert(SpdxSpdxId, ref);
            auto& checksum = obj.insert(JsonIdChecksums, Json::Array());
            auto& checksum1 = checksum.push_back(Json::Object());
            checksum1.insert(JsonIdAlgorithm, JsonIdAllCapsSHA256);
            checksum1.insert(SpdxChecksumValue, hash);
            obj.insert(SpdxLicenseConcluded, SpdxNoAssertion);
            obj.insert(SpdxCopyrightText, SpdxNoAssertion);
            {
                auto& rel = rels.push_back(Json::Object());
                rel.insert(SpdxElementId, ref);
                rel.insert(SpdxRelationshipType, SpdxContainedBy);
                rel.insert(SpdxRelatedSpdxElement, SpdxRefPort);
            }
            if (path == FileVcpkgDotJson)
            {
                auto& rel = rels.push_back(Json::Object());
                rel.insert(SpdxElementId, ref);
                rel.insert(SpdxRelationshipType, SpdxDependencyManifestOf);
                rel.insert(SpdxRelatedSpdxElement, SpdxRefPort);
            }
        }
    }

    for (auto&& rdoc : resource_docs)
    {
        append_move_if_exists_and_array(rels, rdoc, JsonIdRelationships);
        append_move_if_exists_and_array(files, rdoc, JsonIdFiles);
        append_move_if_exists_and_array(packages, rdoc, JsonIdPackages);
    }

    return Json::stringify(doc);
}
