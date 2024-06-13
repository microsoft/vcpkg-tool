#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

static std::string fix_ref_version(StringView ref, StringView version)
{
    return Strings::replace_all(ref, "${VERSION}", version);
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
        if (p->is_array())
        {
            for (auto& e : p->array(VCPKG_LINE_INFO))
            {
                out.push_back(std::move(e));
            }
        }
    }
}

static StringView find_cmake_invocation(StringView contents, StringView command)
{
    auto it = Strings::case_insensitive_ascii_search(contents, command);
    if (it == contents.end()) return {};
    it += command.size();
    if (it == contents.end()) return {};
    if (ParserBase::is_word_char(*it)) return {};
    auto it_end = std::find(it, contents.end(), ')');
    return {it, it_end};
}

static StringView extract_cmake_invocation_argument(StringView command, StringView argument)
{
    auto it = Util::search_and_skip(command.begin(), command.end(), argument);
    it = std::find_if_not(it, command.end(), ParserBase::is_whitespace);
    if (it == command.end()) return {};
    if (*it == '"')
    {
        return {it + 1, std::find(it + 1, command.end(), '"')};
    }
    return {it,
            std::find_if(it + 1, command.end(), [](char ch) { return ParserBase::is_whitespace(ch) || ch == ')'; })};
}

static Json::Object make_resource(
    std::string spdxid, std::string name, std::string downloadLocation, StringView sha512, StringView filename)
{
    Json::Object obj;
    obj.insert(SpdxSpdxId, std::move(spdxid));
    obj.insert(JsonIdName, std::move(name));
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

Json::Value vcpkg::run_resource_heuristics(StringView contents, StringView version_text)
{
    // These are a sequence of heuristics to enable proof-of-concept extraction of remote resources for SPDX SBOM
    // inclusion
    size_t n = 0;
    Json::Object ret;
    auto& packages = ret.insert(JsonIdPackages, Json::Array{});
    auto github = find_cmake_invocation(contents, "vcpkg_from_github");
    if (!github.empty())
    {
        auto repo = extract_cmake_invocation_argument(github, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(github, CMakeVariableRef), version_text);
        auto sha = extract_cmake_invocation_argument(github, CMakeVariableSHA512);

        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", ++n),
                                         repo.to_string(),
                                         fmt::format("git+https://github.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
    }
    auto git = find_cmake_invocation(contents, "vcpkg_from_git");
    if (!git.empty())
    {
        auto url = extract_cmake_invocation_argument(github, CMakeVariableUrl);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(github, CMakeVariableRef), version_text);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", ++n), url.to_string(), fmt::format("git+{}@{}", url, ref), {}, {}));
    }
    auto distfile = find_cmake_invocation(contents, "vcpkg_download_distfile");
    if (!distfile.empty())
    {
        auto url = extract_cmake_invocation_argument(distfile, CMakeVariableUrls);
        auto filename = extract_cmake_invocation_argument(distfile, CMakeVariableFilename);
        auto sha = extract_cmake_invocation_argument(distfile, CMakeVariableSHA512);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", ++n), filename.to_string(), url.to_string(), sha, filename));
    }
    auto sfg = find_cmake_invocation(contents, "vcpkg_from_sourceforge");
    if (!sfg.empty())
    {
        auto repo = extract_cmake_invocation_argument(sfg, CMakeVariableRepo);
        auto ref = fix_ref_version(extract_cmake_invocation_argument(sfg, CMakeVariableRef), version_text);
        auto filename = extract_cmake_invocation_argument(sfg, CMakeVariableFilename);
        auto sha = extract_cmake_invocation_argument(sfg, CMakeVariableSHA512);
        auto url = Strings::concat("https://sourceforge.net/projects/", repo, "/files/", ref, '/', filename);
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", ++n), filename.to_string(), std::move(url), sha, filename));
    }
    return Json::Value::object(std::move(ret));
}

std::string vcpkg::create_spdx_sbom(const InstallPlanAction& action,
                                    View<Path> relative_paths,
                                    View<std::string> hashes,
                                    std::string created_time,
                                    std::string document_namespace,
                                    std::vector<Json::Value>&& resource_docs)
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
    doc.insert(JsonIdName, Strings::concat(action.spec.to_string(), '@', cpgh.version.to_string(), ' ', abi));
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
        if (!rdoc.is_object()) continue;
        auto robj = std::move(rdoc).object(VCPKG_LINE_INFO);
        append_move_if_exists_and_array(rels, robj, JsonIdRelationships);
        append_move_if_exists_and_array(files, robj, JsonIdFiles);
        append_move_if_exists_and_array(packages, robj, JsonIdPackages);
    }

    return Json::stringify(doc);
}
