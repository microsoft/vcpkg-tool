#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

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

std::string vcpkg::create_spdx_sbom(const InstallPlanAction& action,
                                    View<Path> relative_paths,
                                    View<std::string> hashes,
                                    View<Path> relative_package_paths,
                                    View<std::string> package_hashes,
                                    std::string created_time,
                                    std::string document_namespace,
                                    Json::Object&& resource_doc)
{
    Checks::check_exit(VCPKG_LINE_INFO, relative_paths.size() == hashes.size());
    Checks::check_exit(VCPKG_LINE_INFO, relative_package_paths.size() == package_hashes.size());

    const auto& scfl = action.source_control_file_and_location();
    const auto& cpgh = *scfl.source_control_file->core_paragraph;
    StringView abi{SpdxNone};
    if (auto package_abi = action.package_abi())
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
            auto& external_refs = obj.insert(SpdxExternalRefs, Json::Array());
            auto& purl_ref = external_refs.push_back(Json::Object());
            purl_ref.insert(SpdxExternalReferenceCategory, SpdxExternalReferenceCategoryPackageManager);
            purl_ref.insert(SpdxExternalReferenceType, SpdxExternalReferenceTypePurl);
            purl_ref.insert(SpdxExternalReferenceLocator, make_vcpkg_purl(action));
        }
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

    append_move_if_exists_and_array(packages, resource_doc, JsonIdPackages);

    return Json::stringify(doc);
}

std::string vcpkg::calculate_spdx_license(const InstallPlanAction& action)
{
    const auto& scfl = action.source_control_file_and_location();
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

        const auto* feature = scf.find_feature(feature_name);
        Checks::check_exit(VCPKG_LINE_INFO, feature != nullptr);
        const auto& feature_appplicable_licenses = feature->license.applicable_licenses();
        all_licenses_undeclared &= feature->license.kind() == SpdxLicenseDeclarationKind::NotPresent;
        any_explicitly_null_licenses |= feature->license.kind() == SpdxLicenseDeclarationKind::Null;
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
