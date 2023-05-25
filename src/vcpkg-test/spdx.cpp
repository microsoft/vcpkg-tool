#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

TEST_CASE ("spdx maximum serialization", "[spdx]")
{
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    scfl.registry_location = "git://some-vcs-url";
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.summary = {"summary"};
    cpgh.description = {"description"};
    cpgh.homepage = "homepage";
    cpgh.license = "MIT";
    cpgh.port_version = 5;
    cpgh.raw_version = "1.0";
    cpgh.version_scheme = VersionScheme::Relaxed;

    InstallPlanAction ipa(spec, scfl, RequestType::USER_REQUESTED, Test::X86_WINDOWS, {}, {});
    auto& abi = *(ipa.abi_info = AbiInfo{}).get();
    abi.package_abi = "ABIHASH";

    const auto sbom =
        create_spdx_sbom(ipa,
                         std::vector<Path>{"vcpkg.json", "portfile.cmake", "patches/patch1.diff"},
                         std::vector<std::string>{"vcpkg.json-hash", "portfile.cmake-hash", "patch1.diff-hash"},
                         "now",
                         "https://test-document-namespace",
                         {});

    auto expected = Json::parse(R"json(
{
  "$schema": "https://raw.githubusercontent.com/spdx/spdx-spec/v2.2.1/schemas/spdx-schema.json",
  "spdxVersion": "SPDX-2.2",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "documentNamespace": "https://test-document-namespace",
  "name": "zlib:arm-uwp@1.0#5 ABIHASH",
  "creationInfo": {
    "creators": [
      "Tool: vcpkg-unknownhash"
    ],
    "created": "now"
  },
  "relationships": [
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "GENERATES",
      "relatedSpdxElement": "SPDXRef-binary"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-file-0"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-file-1"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-file-2"
    },
    {
      "spdxElementId": "SPDXRef-binary",
      "relationshipType": "GENERATED_FROM",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-0",
      "relationshipType": "CONTAINED_BY",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-0",
      "relationshipType": "DEPENDENCY_MANIFEST_OF",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-1",
      "relationshipType": "CONTAINED_BY",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-2",
      "relationshipType": "CONTAINED_BY",
      "relatedSpdxElement": "SPDXRef-port"
    }
  ],
  "packages": [
    {
      "name": "zlib",
      "SPDXID": "SPDXRef-port",
      "versionInfo": "1.0#5",
      "downloadLocation": "git://some-vcs-url",
      "homepage": "homepage",
      "licenseConcluded": "MIT",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "summary": "summary",
      "description": "description",
      "comment": "This is the port (recipe) consumed by vcpkg."
    },
    {
      "name": "zlib:arm-uwp",
      "SPDXID": "SPDXRef-binary",
      "versionInfo": "ABIHASH",
      "downloadLocation": "NONE",
      "licenseConcluded": "MIT",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "comment": "This is a binary package built by vcpkg."
    }
  ],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-file-0",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "vcpkg.json-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "fileName": "./portfile.cmake",
      "SPDXID": "SPDXRef-file-1",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "portfile.cmake-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "fileName": "./patches/patch1.diff",
      "SPDXID": "SPDXRef-file-2",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "patch1.diff-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    }
  ]
})json")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom).value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}

TEST_CASE ("spdx minimum serialization", "[spdx]")
{
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.port_version = 0;
    cpgh.raw_version = "1.0";
    cpgh.version_scheme = VersionScheme::String;

    InstallPlanAction ipa(spec, scfl, RequestType::USER_REQUESTED, Test::X86_WINDOWS, {}, {});
    auto& abi = *(ipa.abi_info = AbiInfo{}).get();
    abi.package_abi = "deadbeef";

    const auto sbom = create_spdx_sbom(ipa,
                                       std::vector<Path>{"vcpkg.json", "portfile.cmake"},
                                       std::vector<std::string>{"hash-vcpkg.json", "hash-portfile.cmake"},
                                       "now+1",
                                       "https://test-document-namespace-2",
                                       {});

    auto expected = Json::parse(R"json(
{
  "$schema": "https://raw.githubusercontent.com/spdx/spdx-spec/v2.2.1/schemas/spdx-schema.json",
  "spdxVersion": "SPDX-2.2",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "documentNamespace": "https://test-document-namespace-2",
  "name": "zlib:arm-uwp@1.0 deadbeef",
  "creationInfo": {
    "creators": [
      "Tool: vcpkg-unknownhash"
    ],
    "created": "now+1"
  },
  "relationships": [
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "GENERATES",
      "relatedSpdxElement": "SPDXRef-binary"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-file-0"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-file-1"
    },
    {
      "spdxElementId": "SPDXRef-binary",
      "relationshipType": "GENERATED_FROM",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-0",
      "relationshipType": "CONTAINED_BY",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-0",
      "relationshipType": "DEPENDENCY_MANIFEST_OF",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-file-1",
      "relationshipType": "CONTAINED_BY",
      "relatedSpdxElement": "SPDXRef-port"
    }
  ],
  "packages": [
    {
      "name": "zlib",
      "SPDXID": "SPDXRef-port",
      "versionInfo": "1.0",
      "downloadLocation": "NOASSERTION",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "comment": "This is the port (recipe) consumed by vcpkg."
    },
    {
      "name": "zlib:arm-uwp",
      "SPDXID": "SPDXRef-binary",
      "versionInfo": "deadbeef",
      "downloadLocation": "NONE",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "comment": "This is a binary package built by vcpkg."
    }
  ],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-file-0",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "hash-vcpkg.json"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "fileName": "./portfile.cmake",
      "SPDXID": "SPDXRef-file-1",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "hash-portfile.cmake"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    }
  ]
})json")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom).value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}

TEST_CASE ("spdx concat resources", "[spdx]")
{
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.port_version = 0;
    cpgh.raw_version = "1.0";
    cpgh.version_scheme = VersionScheme::String;

    InstallPlanAction ipa(spec, scfl, RequestType::USER_REQUESTED, Test::X86_WINDOWS, {}, {});
    auto& abi = *(ipa.abi_info = AbiInfo{}).get();
    abi.package_abi = "deadbeef";

    auto doc1 = Json::parse(R"json(
{
  "relationships": [ "r1", "r2", "r3" ],
  "files": [ "f1", "f2", "f3" ]
})json")
                    .value(VCPKG_LINE_INFO)
                    .value;
    auto doc2 = Json::parse(R"json(
{
  "packages": [ "p1", "p2", "p3" ],
  "files": [ "f4", "f5" ]
})json")
                    .value(VCPKG_LINE_INFO)
                    .value;

    const auto sbom = create_spdx_sbom(ipa, {}, {}, "now+1", "ns", {std::move(doc1), std::move(doc2)});

    auto expected = Json::parse(R"json(
{
  "$schema": "https://raw.githubusercontent.com/spdx/spdx-spec/v2.2.1/schemas/spdx-schema.json",
  "spdxVersion": "SPDX-2.2",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "documentNamespace": "ns",
  "name": "zlib:arm-uwp@1.0 deadbeef",
  "creationInfo": {
    "creators": [
      "Tool: vcpkg-unknownhash"
    ],
    "created": "now+1"
  },
  "relationships": [
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "GENERATES",
      "relatedSpdxElement": "SPDXRef-binary"
    },
    {
      "spdxElementId": "SPDXRef-binary",
      "relationshipType": "GENERATED_FROM",
      "relatedSpdxElement": "SPDXRef-port"
    },
    "r1",
    "r2",
    "r3"
  ],
  "packages": [
    {
      "name": "zlib",
      "SPDXID": "SPDXRef-port",
      "versionInfo": "1.0",
      "downloadLocation": "NOASSERTION",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "comment": "This is the port (recipe) consumed by vcpkg."
    },
    {
      "name": "zlib:arm-uwp",
      "SPDXID": "SPDXRef-binary",
      "versionInfo": "deadbeef",
      "downloadLocation": "NONE",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "comment": "This is a binary package built by vcpkg."
    },
    "p1",
    "p2",
    "p3"
  ],
  "files": [
    "f1",
    "f2",
    "f3",
    "f4",
    "f5"
  ]
})json")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom).value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}
