#include <catch2/catch.hpp>

#include <vcpkg/commands.dependinfo.h>

using namespace vcpkg::Commands::DependInfo;

namespace
{
    const auto DOT_TEMPLATE =
        "digraph G{{ rankdir=LR; edge [minlen=3]; overlap=false;{}empty [label=\"{} singletons...\"]; }}";

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
        CHECK(create_dot_as_string(single_node_dependencies()) == fmt::format(DOT_TEMPLATE, "a;a -> a;", 0));
    }

    SECTION ("4 nodes")
    {
        CHECK(create_dot_as_string(four_nodes_dependencies()) ==
              fmt::format(DOT_TEMPLATE, "a;a -> b;a -> c;a -> d;b;b -> c;c;c -> d;", 1));
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
