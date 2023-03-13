#include <vcpkg/commands.version.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

static std::string conclude_license(const std::string& license)
{
    if (license.empty()) return "NOASSERTION";
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
    obj.insert("SPDXID", std::move(spdxid));
    obj.insert("name", std::move(name));
    if (!filename.empty())
    {
        obj.insert("packageFileName", filename);
    }
    obj.insert("downloadLocation", std::move(downloadLocation));
    obj.insert("licenseConcluded", "NOASSERTION");
    obj.insert("licenseDeclared", "NOASSERTION");
    obj.insert("copyrightText", "NOASSERTION");
    if (!sha512.empty())
    {
        auto& chk = obj.insert("checksums", Json::Array());
        auto& chk512 = chk.push_back(Json::Object());
        chk512.insert("algorithm", "SHA512");
        chk512.insert("checksumValue", Strings::ascii_to_lowercase(sha512.to_string()));
    }
    return obj;
}

Json::Value vcpkg::run_resource_heuristics(StringView contents)
{
    // These are a sequence of heuristics to enable proof-of-concept extraction of remote resources for SPDX SBOM
    // inclusion
    size_t n = 0;
    Json::Object ret;
    auto& packages = ret.insert("packages", Json::Array{});
    auto github = find_cmake_invocation(contents, "vcpkg_from_github");
    if (!github.empty())
    {
        auto repo = extract_cmake_invocation_argument(github, "REPO");
        auto ref = extract_cmake_invocation_argument(github, "REF");
        auto sha = extract_cmake_invocation_argument(github, "SHA512");
        packages.push_back(make_resource(fmt::format("SPDXRef-resource-{}", ++n),
                                         repo.to_string(),
                                         fmt::format("git+https://github.com/{}@{}", repo, ref),
                                         sha,
                                         {}));
    }
    auto git = find_cmake_invocation(contents, "vcpkg_from_git");
    if (!git.empty())
    {
        auto url = extract_cmake_invocation_argument(github, "URL");
        auto ref = extract_cmake_invocation_argument(github, "REF");
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", ++n), url.to_string(), fmt::format("git+{}@{}", url, ref), {}, {}));
    }
    auto distfile = find_cmake_invocation(contents, "vcpkg_download_distfile");
    if (!distfile.empty())
    {
        auto url = extract_cmake_invocation_argument(distfile, "URLS");
        auto filename = extract_cmake_invocation_argument(distfile, "FILENAME");
        auto sha = extract_cmake_invocation_argument(distfile, "SHA512");
        packages.push_back(make_resource(
            fmt::format("SPDXRef-resource-{}", ++n), filename.to_string(), url.to_string(), sha, filename));
    }
    auto sfg = find_cmake_invocation(contents, "vcpkg_from_sourceforge");
    if (!sfg.empty())
    {
        auto repo = extract_cmake_invocation_argument(sfg, "REPO");
        auto ref = extract_cmake_invocation_argument(sfg, "REF");
        auto filename = extract_cmake_invocation_argument(sfg, "FILENAME");
        auto sha = extract_cmake_invocation_argument(sfg, "SHA512");
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

    const std::string noassert = "NOASSERTION";
    const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
    const auto& cpgh = *scfl.source_control_file->core_paragraph;
    const StringView abi = action.package_abi() ? StringView(*action.package_abi().get()) : "NONE";

    Json::Object doc;
    doc.insert("$schema", "https://raw.githubusercontent.com/spdx/spdx-spec/v2.2.1/schemas/spdx-schema.json");
    doc.insert("spdxVersion", "SPDX-2.2");
    doc.insert("dataLicense", "CC0-1.0");
    doc.insert("SPDXID", "SPDXRef-DOCUMENT");
    doc.insert("documentNamespace", std::move(document_namespace));
    doc.insert("name", Strings::concat(action.spec.to_string(), '@', cpgh.to_version().to_string(), ' ', abi));
    {
        auto& cinfo = doc.insert("creationInfo", Json::Object());
        auto& creators = cinfo.insert("creators", Json::Array());
        creators.push_back(Strings::concat("Tool: vcpkg-", VCPKG_VERSION_AS_STRING));
        cinfo.insert("created", std::move(created_time));
    }

    auto& rels = doc.insert("relationships", Json::Array());
    auto& packages = doc.insert("packages", Json::Array());
    {
        auto& obj = packages.push_back(Json::Object());
        obj.insert("name", action.spec.name());
        obj.insert("SPDXID", "SPDXRef-port");
        obj.insert("versionInfo", cpgh.to_version().to_string());
        obj.insert("downloadLocation", scfl.registry_location.empty() ? noassert : scfl.registry_location);
        if (!cpgh.homepage.empty())
        {
            obj.insert("homepage", cpgh.homepage);
        }
        obj.insert("licenseConcluded", conclude_license(cpgh.license.value_or("")));
        obj.insert("licenseDeclared", noassert);
        obj.insert("copyrightText", noassert);
        if (!cpgh.summary.empty()) obj.insert("summary", Strings::join("\n", cpgh.summary));
        if (!cpgh.description.empty()) obj.insert("description", Strings::join("\n", cpgh.description));
        obj.insert("comment", "This is the port (recipe) consumed by vcpkg.");
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert("spdxElementId", "SPDXRef-port");
            rel.insert("relationshipType", "GENERATES");
            rel.insert("relatedSpdxElement", "SPDXRef-binary");
        }
        for (size_t i = 0; i < relative_paths.size(); ++i)
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert("spdxElementId", "SPDXRef-port");
            rel.insert("relationshipType", "CONTAINS");
            rel.insert("relatedSpdxElement", fmt::format("SPDXRef-file-{}", i));
        }
    }
    {
        auto& obj = packages.push_back(Json::Object());
        obj.insert("name", action.spec.to_string());
        obj.insert("SPDXID", "SPDXRef-binary");
        obj.insert("versionInfo", abi);
        obj.insert("downloadLocation", "NONE");
        obj.insert("licenseConcluded", conclude_license(cpgh.license.value_or("")));
        obj.insert("licenseDeclared", noassert);
        obj.insert("copyrightText", noassert);
        obj.insert("comment", "This is a binary package built by vcpkg.");
        {
            auto& rel = rels.push_back(Json::Object());
            rel.insert("spdxElementId", "SPDXRef-binary");
            rel.insert("relationshipType", "GENERATED_FROM");
            rel.insert("relatedSpdxElement", "SPDXRef-port");
        }
    }

    auto& files = doc.insert("files", Json::Array());
    {
        for (size_t i = 0; i < relative_paths.size(); ++i)
        {
            const auto& path = relative_paths[i];
            const auto& hash = hashes[i];

            auto& obj = files.push_back(Json::Object());
            obj.insert("fileName", "./" + path.generic_u8string());
            const auto ref = fmt::format("SPDXRef-file-{}", i);
            obj.insert("SPDXID", ref);
            auto& checksum = obj.insert("checksums", Json::Array());
            auto& checksum1 = checksum.push_back(Json::Object());
            checksum1.insert("algorithm", "SHA256");
            checksum1.insert("checksumValue", hash);
            obj.insert("licenseConcluded", noassert);
            obj.insert("copyrightText", noassert);
            {
                auto& rel = rels.push_back(Json::Object());
                rel.insert("spdxElementId", ref);
                rel.insert("relationshipType", "CONTAINED_BY");
                rel.insert("relatedSpdxElement", "SPDXRef-port");
            }
            if (path == "vcpkg.json")
            {
                auto& rel = rels.push_back(Json::Object());
                rel.insert("spdxElementId", ref);
                rel.insert("relationshipType", "DEPENDENCY_MANIFEST_OF");
                rel.insert("relatedSpdxElement", "SPDXRef-port");
            }
        }
    }

    for (auto&& rdoc : resource_docs)
    {
        if (!rdoc.is_object()) continue;
        auto robj = std::move(rdoc).object(VCPKG_LINE_INFO);
        append_move_if_exists_and_array(rels, robj, "relationships");
        append_move_if_exists_and_array(files, robj, "files");
        append_move_if_exists_and_array(packages, robj, "packages");
    }

    return Json::stringify(doc);
}
