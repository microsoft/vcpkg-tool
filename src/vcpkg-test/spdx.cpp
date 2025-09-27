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

TEST_CASE ("spdx run resource heuristics", "[spdx]")
{
    auto portfile_cmake = R"(
vcpkg_download_distfile(ARCHIVE
    URLS "https://vcpkg-download-distfile.dev/${VERSION}.tar.gz"
         "https://vcpkg-download-distfile.dev/${VERSION}-other.tar.gz"
    FILENAME "distfile-${VERSION}.tar.gz"
    SHA512 distfile_test_1
)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO from/github
    REF v${VERSION}
    SHA512 from_github_test_1
    HEAD_REF devel
)
vcpkg_from_gitlab(
    OUT_SOURCE_PATH SOURCE_PATH
    GITLAB_URL https://from.gitlab.org
    REPO from/gitlab
    REF "${VERSION}"
    SHA512 from_gitlab_test_1
)
vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sourceforge
    REF sourceforge
    FILENAME "sourceforge-${VERSION}.tar.gz"
    SHA512 sourceforge_test_1
    )
vcpkg_from_bitbucket(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO from/bitbucket
    REF "v${VERSION}"
    SHA512 from_bitbucket_test_1
    HEAD_REF master
)
vcpkg_download_distfile(ARCHIVE
    URLS "https://vcpkg-download-distfile.dev/${VERSION}.tar.gz"
         "https://vcpkg-download-distfile.dev/${VERSION}-other.tar.gz"
    FILENAME "distfile-${VERSION}.tar.gz"
    SHA512 distfile_test_2
)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO from/github
    REF v${VERSION}
    SHA512 from_github_test_2
    HEAD_REF devel
)
vcpkg_from_gitlab(
    OUT_SOURCE_PATH SOURCE_PATH
    GITLAB_URL https://from.gitlab.org
    REPO from/gitlab
    REF "${VERSION}"
    SHA512 from_gitlab_test_2
)
vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sourceforge
    REF sourceforge
    FILENAME "sourceforge-${VERSION}.tar.gz"
    SHA512 sourceforge_test_2
    )
vcpkg_from_bitbucket(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO from/bitbucket
    REF "v${VERSION}"
    SHA512 from_bitbucket_test_2
    HEAD_REF master
)
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://from-git-1.dev
    REF "${VERSION}"
    HEAD_REF main
)
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://from-git-2.dev
    REF "${VERSION}"
    HEAD_REF main
)
    )";
    auto expected = Json::parse(R"json(
{
  "packages": [
    {
      "SPDXID": "SPDXRef-resource-0",
      "name": "from/github",
      "downloadLocation": "git+https://github.com/from/github@v3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_github_test_1"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-1",
      "name": "from/github",
      "downloadLocation": "git+https://github.com/from/github@v3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_github_test_2"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-2",
      "name": "from/gitlab",
      "downloadLocation": "git+https://from.gitlab.org/from/gitlab@3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_gitlab_test_1"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-3",
      "name": "from/gitlab",
      "downloadLocation": "git+https://from.gitlab.org/from/gitlab@3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_gitlab_test_2"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-4",
      "name": "https://from-git-1.dev",
      "downloadLocation": "git+https://from-git-1.dev@3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "SPDXID": "SPDXRef-resource-5",
      "name": "https://from-git-2.dev",
      "downloadLocation": "git+https://from-git-2.dev@3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "SPDXID": "SPDXRef-resource-6",
      "name": "distfile-3.2.1.tar.gz",
      "packageFileName": "distfile-3.2.1.tar.gz",
      "downloadLocation": "https://vcpkg-download-distfile.dev/3.2.1.tar.gz",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "distfile_test_1"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-7",
      "name": "distfile-3.2.1.tar.gz",
      "packageFileName": "distfile-3.2.1.tar.gz",
      "downloadLocation": "https://vcpkg-download-distfile.dev/3.2.1.tar.gz",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "distfile_test_2"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-8",
      "name": "sourceforge-3.2.1.tar.gz",
      "packageFileName": "sourceforge-3.2.1.tar.gz",
      "downloadLocation": "https://sourceforge.net/projects/sourceforge/files/sourceforge/sourceforge-3.2.1.tar.gz",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "sourceforge_test_1"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-9",
      "name": "sourceforge-3.2.1.tar.gz",
      "packageFileName": "sourceforge-3.2.1.tar.gz",
      "downloadLocation": "https://sourceforge.net/projects/sourceforge/files/sourceforge/sourceforge-3.2.1.tar.gz",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "sourceforge_test_2"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-10",
      "name": "from/bitbucket",
      "downloadLocation": "git+https://bitbucket.com/from/bitbucket@v3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_bitbucket_test_1"
        }
      ]
    },
    {
      "SPDXID": "SPDXRef-resource-11",
      "name": "from/bitbucket",
      "downloadLocation": "git+https://bitbucket.com/from/bitbucket@v3.2.1",
      "licenseConcluded": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "copyrightText": "NOASSERTION",
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "from_bitbucket_test_2"
        }
      ]
    }
  ]
})json",
                                "test")
                        .value(VCPKG_LINE_INFO);

    auto generated_spdx = run_resource_heuristics(portfile_cmake, "3.2.1");
    auto spdx_str = Json::stringify(generated_spdx);
    auto res = Json::parse(spdx_str, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, res.value);
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
    cpgh.license = parse_spdx_license_expression_required("MIT");
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
                         std::vector<Path>{"include/zlib.h", "lib/zlib.lib"},
                         std::vector<std::string>{"zlib-header-hash", "zlib-lib-hash"},
                         "now",
                         "https://test-document-namespace",
                         {});

    static constexpr StringLiteral expected_text = R"json(
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
      "relatedSpdxElement": "SPDXRef-port-file-0"
    },
    {
      "spdxElementId": "SPDXRef-port-file-0",
      "relationshipType": "DEPENDENCY_MANIFEST_OF",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-port-file-1"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-port-file-2"
    },
    {
      "spdxElementId": "SPDXRef-binary",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-binary-file-0"
    },
    {
      "spdxElementId": "SPDXRef-binary",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-binary-file-1"
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
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
      "SPDXID": "SPDXRef-port-file-2",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "patch1.diff-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "fileName": "./include/zlib.h",
      "SPDXID": "SPDXRef-binary-file-0",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "zlib-header-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    },
    {
      "fileName": "./lib/zlib.lib",
      "SPDXID": "SPDXRef-binary-file-1",
      "checksums": [
        {
          "algorithm": "SHA256",
          "checksumValue": "zlib-lib-hash"
        }
      ],
      "licenseConcluded": "NOASSERTION",
      "copyrightText": "NOASSERTION"
    }
  ]
})json";

    auto expected = Json::parse(expected_text, "test").value(VCPKG_LINE_INFO);
    auto doc = Json::parse(sbom, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);

    CHECK(read_spdx_license_text(expected_text, "test") == "MIT");
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
                                       {},
                                       {},
                                       "now+1",
                                       "https://test-document-namespace-2",
                                       {});

    static constexpr StringLiteral expected_text = R"json(
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
      "relatedSpdxElement": "SPDXRef-port-file-0"
    },
    {
      "spdxElementId": "SPDXRef-port-file-0",
      "relationshipType": "DEPENDENCY_MANIFEST_OF",
      "relatedSpdxElement": "SPDXRef-port"
    },
    {
      "spdxElementId": "SPDXRef-port",
      "relationshipType": "CONTAINS",
      "relatedSpdxElement": "SPDXRef-port-file-1"
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
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    auto expected = Json::parse(expected_text, "test").value(VCPKG_LINE_INFO);
    auto doc = Json::parse(sbom, "test").value(VCPKG_LINE_INFO);
    Test::check_json_eq(expected.value, doc.value);
    CHECK(!read_spdx_license_text(expected_text, "test").has_value());
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

    const auto sbom = create_spdx_sbom(ipa, {}, {}, {}, {}, "now+1", "ns", {std::move(doc1), std::move(doc2)});

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

TEST_CASE ("spdx license parse edge cases", "[spdx]")
{
    CHECK(!read_spdx_license_text("this is not json", "test").has_value());

    static constexpr StringLiteral missing_packages = R"json(
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
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(missing_packages, "test").has_value());

    static constexpr StringLiteral empty_packages = R"json(
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
  "packages": [],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(empty_packages, "test").has_value());

    static constexpr StringLiteral wrong_packages_type = R"json(
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
  "packages": {},
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(wrong_packages_type, "test").has_value());

    static constexpr StringLiteral wrong_packages_zero_type = R"json(
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
  "packages": [42],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(wrong_packages_zero_type, "test").has_value());

    static constexpr StringLiteral missing_license_block = R"json(
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
      "copyrightText": "NOASSERTION",
      "comment": "This is the port (recipe) consumed by vcpkg."
    }
  ],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(missing_license_block, "test").has_value());

    static constexpr StringLiteral wrong_license_type = R"json(
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
      "licenseConcluded": 42,
      "licenseDeclared": 42,
      "copyrightText": "NOASSERTION",
      "comment": "This is the port (recipe) consumed by vcpkg."
    }
  ],
  "files": [
    {
      "fileName": "./vcpkg.json",
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(wrong_license_type, "test").has_value());

    static constexpr StringLiteral empty_license = R"json(
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
      "licenseConcluded": "",
      "licenseDeclared": "",
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
      "SPDXID": "SPDXRef-port-file-0",
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
      "SPDXID": "SPDXRef-port-file-1",
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
})json";

    CHECK(!read_spdx_license_text(empty_license, "test").has_value());
}
