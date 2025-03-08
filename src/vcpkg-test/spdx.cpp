#include <vcpkg-test/util.h>

#include <vcpkg/dependencies.h>
#include <vcpkg/spdx.h>

using namespace vcpkg;

TEST_CASE ("replace CMake variable", "[spdx]")
{
    static constexpr StringLiteral str{"lorem ip${VERSION}"};
    {
        auto res = replace_cmake_var(str, "VERSION", "sum");
        REQUIRE(res == "lorem ipsum");
    }
    {
        auto res = replace_cmake_var(str, "VERSiON", "sum");
        REQUIRE(res == "lorem ip${VERSION}");
    }
}

TEST_CASE ("extract first cmake invocation args", "[spdx]")
{
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum()", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsummmmm() lorem_ipsum(asdf)", "lorem_ipsum");
        REQUIRE(res == "asdf");
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum(abc)", "lorem_ipsu");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum(abc", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum    (abc)    ", "lorem_ipsum");
        REQUIRE(res == "abc");
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum   x (abc)    ", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipum(abc)", "lorem_ipsum");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum( )", "lorem_ipsum");
        REQUIRE(res == " ");
    }
    {
        auto res = extract_first_cmake_invocation_args("lorem_ipsum_", "lorem_ipsum");
        REQUIRE(res.empty());
    }
}

TEST_CASE ("extract arg from cmake invocation args", "[spdx]")
{
    {
        auto res = extract_arg_from_cmake_invocation_args("loremipsum", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("loremipsum lorem value", "lorem");
        REQUIRE(res == "value");
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("loremipsum lorem value       ", "lorem");
        REQUIRE(res == "value");
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem \"", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem   ", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem ipsum", "lorem");
        REQUIRE(res == "ipsum");
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem \"ipsum", "lorem");
        REQUIRE(res.empty());
    }
    {
        auto res = extract_arg_from_cmake_invocation_args("lorem \"ipsum\"", "lorem");
        REQUIRE(res == "ipsum");
    }
}

TEST_CASE ("spdx maximum serialization", "[spdx]")
{
    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    scfl.spdx_location = "git://some-vcs-url";
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.summary = {"summary"};
    cpgh.description = {"description"};
    cpgh.homepage = "homepage";
    cpgh.license = "MIT";
    cpgh.version_scheme = VersionScheme::Relaxed;
    cpgh.version = Version{"1.0", 5};

    InstallPlanAction ipa(
        spec, scfl, packages_dir_assigner, RequestType::USER_REQUESTED, UseHeadVersion::No, Editable::No, {}, {}, {});
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
      "Tool: vcpkg-2999-12-31-unknownhash"
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
})json",
                                "test")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}

TEST_CASE ("spdx minimum serialization", "[spdx]")
{
    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.version_scheme = VersionScheme::String;
    cpgh.version = Version{"1.0", 0};

    InstallPlanAction ipa(
        spec, scfl, packages_dir_assigner, RequestType::USER_REQUESTED, UseHeadVersion::No, Editable::No, {}, {}, {});
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
      "Tool: vcpkg-2999-12-31-unknownhash"
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
})json",
                                "test")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}

TEST_CASE ("spdx concat resources", "[spdx]")
{
    PackagesDirAssigner packages_dir_assigner{"test_packages_root"};
    PackageSpec spec{"zlib", Test::ARM_UWP};
    SourceControlFileAndLocation scfl;
    auto& scf = *(scfl.source_control_file = std::make_unique<SourceControlFile>());
    auto& cpgh = *(scf.core_paragraph = std::make_unique<SourceParagraph>());
    cpgh.name = "zlib";
    cpgh.version_scheme = VersionScheme::String;
    cpgh.version = Version{"1.0", 0};

    InstallPlanAction ipa(
        spec, scfl, packages_dir_assigner, RequestType::USER_REQUESTED, UseHeadVersion::No, Editable::No, {}, {}, {});
    auto& abi = *(ipa.abi_info = AbiInfo{}).get();
    abi.package_abi = "deadbeef";

    auto doc1 = Json::parse(R"json(
{
  "relationships": [ "r1", "r2", "r3" ],
  "files": [ "f1", "f2", "f3" ]
})json",
                            "test")
                    .value(VCPKG_LINE_INFO)
                    .value.object(VCPKG_LINE_INFO);
    auto doc2 = Json::parse(R"json(
{
  "packages": [ "p1", "p2", "p3" ],
  "files": [ "f4", "f5" ]
})json",
                            "test")
                    .value(VCPKG_LINE_INFO)
                    .value.object(VCPKG_LINE_INFO);

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
      "Tool: vcpkg-2999-12-31-unknownhash"
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
})json",
                                "test")
                        .value(VCPKG_LINE_INFO);

    auto doc = Json::parse(sbom, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
}
