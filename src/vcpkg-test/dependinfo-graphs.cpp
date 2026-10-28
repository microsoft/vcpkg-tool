#include <vcpkg-test/util.h>

#include <vcpkg/commands.depend-info.h>

using namespace vcpkg;

namespace
{
    const auto DOT_TEMPLATE =
        "digraph G{{ rankdir=LR; node [fontname=Sans]; edge [minlen=3]; overlap=false;\n{}\"{} singletons...\";\n}}";

    const auto DGML_TEMPLATE =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><DirectedGraph "
        "xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\"><Nodes>{}</Nodes><Links>{}</Links></DirectedGraph>";

    const auto MERMAID_TEMPLATE = "flowchart TD;{}";

    std::vector<PackageDependInfo> single_node_dependencies() { return {{"a", 0, {}, {"a"}}}; }

    std::vector<PackageDependInfo> four_nodes_dependencies()
    {
        return {{"a", 0, {}, {"b", "c", "d"}}, {"b", 0, {}, {"c"}}, {"c", 0, {}, {"d"}}, {"d", 0, {}, {}}};
    }
}

TEST_CASE ("depend-info DOT graph output", "[depend-info]")
{
    SECTION ("empty")
    {
        CHECK(create_dot_as_string({}) == fmt::format(DOT_TEMPLATE, "", 0));
    }

    SECTION ("single node")
    {
        CHECK(create_dot_as_string(single_node_dependencies()) ==
              fmt::format(DOT_TEMPLATE, "\"a\";\n\"a\" -> \"a\";\n", 0));
    }

    SECTION ("4 nodes")
    {
        CHECK(create_dot_as_string(four_nodes_dependencies()) ==
              fmt::format(DOT_TEMPLATE,
                          "\"a\";\n\"a\" -> \"b\";\n\"a\" -> \"c\";\n\"a\" -> \"d\";\n\"b\";\n\"b\" -> "
                          "\"c\";\n\"c\";\n\"c\" -> \"d\";\n\"d\";\n",
                          1));
    }
}

TEST_CASE ("depend-info DGML graph output", "[depend-info]")
{
    SECTION ("empty")
    {
        CHECK(create_dgml_as_string({}) == fmt::format(DGML_TEMPLATE, "", ""));
    }

    SECTION ("single node")
    {
        CHECK(create_dgml_as_string(single_node_dependencies()) ==
              fmt::format(DGML_TEMPLATE, "<Node Id=\"a\"/>", "<Link Source=\"a\" Target=\"a\"/>"));
    }

    SECTION ("4 nodes")
    {
        CHECK(create_dgml_as_string(four_nodes_dependencies()) ==
              fmt::format(DGML_TEMPLATE,
                          "<Node Id=\"a\"/><Node Id=\"b\"/><Node Id=\"c\"/><Node Id=\"d\"/>",
                          "<Link Source=\"a\" Target=\"b\"/><Link Source=\"a\" Target=\"c\"/><Link Source=\"a\" "
                          "Target=\"d\"/><Link Source=\"b\" Target=\"c\"/><Link Source=\"c\" Target=\"d\"/>"));
    }
}

TEST_CASE ("depend-info mermaid graph output", "[depend-info]")
{
    SECTION ("empty")
    {
        CHECK(create_mermaid_as_string({}) == fmt::format(MERMAID_TEMPLATE, ""));
    }

    SECTION ("single node")
    {
        CHECK(create_mermaid_as_string(single_node_dependencies()) == fmt::format(MERMAID_TEMPLATE, " a --> a;"));
    }

    SECTION ("4 nodes")
    {
        CHECK(create_mermaid_as_string(four_nodes_dependencies()) ==
              fmt::format(MERMAID_TEMPLATE, " a --> b; a --> c; a --> d; b --> c; c --> d;"));
    }
}

namespace
{
    DependInfoPackage make_package(StringView name, Triplet triplet, std::vector<DependInfoDependency> dependencies)
    {
        DependInfoPackage package;
        package.spec = PackageSpec{name.to_string(), triplet};
        package.version = Version{"1", 0};
        package.depth = 0;
        package.dependencies = std::move(dependencies);
        return package;
    }

    std::vector<std::string> host_only_packages(const std::vector<DependInfoPackage>& packages)
    {
        std::vector<std::string> result;
        for (auto&& package : packages)
        {
            if (package.host_only)
            {
                result.push_back(package.spec.to_string());
            }
        }

        return result;
    }
}

TEST_CASE ("depend-info host-only packages", "[depend-info]")
{
    const auto target = Triplet::from_canonical_name("x64-target");
    const auto host = Triplet::from_canonical_name("x64-host");

    SECTION ("host triplet is target triplet")
    {
        // "both" is a host and a non-host dependency of "a", "c" is a host dependency of "a" and a non-host dependency
        // of "b"
        std::vector<DependInfoPackage> packages{
            make_package(
                "a",
                target,
                {{{"b", target}, false}, {{"both", target}, false}, {{"c", target}, true}, {{"tool", target}, true}}),
            make_package("b", target, {{{"c", target}, false}}),
            make_package("both", target, {}),
            make_package("c", target, {}),
            make_package("tool", target, {{{"tool-dep", target}, false}}),
            make_package("tool-dep", target, {}),
            make_package("toplevel-tool", target, {}),
        };

        const std::vector<PackageSpec> roots{{"a", target}, {"not-in-plan", target}};
        mark_host_only_packages(packages, roots);
        CHECK(host_only_packages(packages) ==
              std::vector<std::string>{"tool:x64-target", "tool-dep:x64-target", "toplevel-tool:x64-target"});
    }

    SECTION ("host triplet is not target triplet")
    {
        std::vector<DependInfoPackage> packages{
            make_package("a", target, {{{"both", target}, false}, {{"both", host}, true}, {{"tool", host}, true}}),
            make_package("both", target, {}),
            make_package("both", host, {}),
            make_package("tool", host, {{{"tool-dep", host}, false}}),
            make_package("tool-dep", host, {}),
        };

        const std::vector<PackageSpec> roots{{"a", target}};
        mark_host_only_packages(packages, roots);
        CHECK(host_only_packages(packages) ==
              std::vector<std::string>{"both:x64-host", "tool:x64-host", "tool-dep:x64-host"});
    }

    SECTION ("no roots")
    {
        std::vector<DependInfoPackage> packages{make_package("a", target, {})};
        packages[0].host_only = false;
        mark_host_only_packages(packages, {});
        CHECK(host_only_packages(packages) == std::vector<std::string>{"a:x64-target"});
    }
}

TEST_CASE ("depend-info JSON output", "[depend-info]")
{
    SECTION ("empty")
    {
        CHECK(create_json_as_string({}) == "{}\n");
    }

    SECTION ("packages")
    {
        const auto target = Triplet::from_canonical_name("x64-target");
        const auto host = Triplet::from_canonical_name("x64-host");
        std::vector<DependInfoPackage> packages{
            make_package("a", target, {{{"b", target}, false}, {{"tool", host}, true}}),
            make_package("b", target, {}),
            make_package("tool", host, {}),
        };

        packages[0].version = Version{"1.2.3", 4};
        packages[0].features = {"feature-1", "feature-2"};
        packages[0].license = "MIT AND BSD-3-Clause";
        packages[0].homepage = "https://example.com/a";
        packages[0].host_only = false;
        packages[1].depth = 1;
        packages[1].license = "LicenseRef-vcpkg-null";
        packages[1].host_only = false;
        packages[2].depth = 1;

        CHECK(create_json_as_string(packages) == R"json({
  "a:x64-target": {
    "name": "a",
    "triplet": "x64-target",
    "version": "1.2.3",
    "port-version": 4,
    "features": [
      "feature-1",
      "feature-2"
    ],
    "depth": 0,
    "host-only": false,
    "license": "MIT AND BSD-3-Clause",
    "homepage": "https://example.com/a",
    "dependencies": [
      {
        "name": "b",
        "triplet": "x64-target",
        "host": false
      },
      {
        "name": "tool",
        "triplet": "x64-host",
        "host": true
      }
    ]
  },
  "b:x64-target": {
    "name": "b",
    "triplet": "x64-target",
    "version": "1",
    "port-version": 0,
    "features": [],
    "depth": 1,
    "host-only": false,
    "license": "LicenseRef-vcpkg-null",
    "dependencies": []
  },
  "tool:x64-host": {
    "name": "tool",
    "triplet": "x64-host",
    "version": "1",
    "port-version": 0,
    "features": [],
    "depth": 1,
    "host-only": true,
    "license": null,
    "dependencies": []
  }
}
)json");
    }
}
