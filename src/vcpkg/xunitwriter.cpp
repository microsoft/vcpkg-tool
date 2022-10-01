#include <vcpkg/base/chrono.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/build.h>
#include <vcpkg/xunitwriter.h>

using namespace vcpkg;

struct vcpkg::XunitTest
{
    std::string name;
    std::string method;
    std::string owner;
    BuildResult result;
    ElapsedTime time;
    std::chrono::system_clock::time_point start_time;
    std::string abi_tag;
    std::vector<std::string> features;
};

namespace
{
    static void xml_test(XmlSerializer& xml, const XunitTest& test)
    {
        StringLiteral result_string = "";
        switch (test.result)
        {
            case BuildResult::POST_BUILD_CHECKS_FAILED:
            case BuildResult::FILE_CONFLICTS:
            case BuildResult::BUILD_FAILED: result_string = "Fail"; break;
            case BuildResult::EXCLUDED:
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES: result_string = "Skip"; break;
            case BuildResult::SUCCEEDED: result_string = "Pass"; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        xml.start_complex_open_tag("test")
            .attr("name", test.name)
            .attr("method", test.method)
            .attr("time", test.time.as<std::chrono::seconds>().count())
            .attr("result", result_string)
            .finish_complex_open_tag()
            .line_break();
        xml.open_tag("traits").line_break();
        if (!test.abi_tag.empty())
        {
            xml.start_complex_open_tag("trait")
                .attr("name", "abi_tag")
                .attr("value", test.abi_tag)
                .finish_self_closing_complex_tag()
                .line_break();
        }

        if (!test.features.empty())
        {
            xml.start_complex_open_tag("trait")
                .attr("name", "features")
                .attr("value", Strings::join(", ", test.features))
                .finish_self_closing_complex_tag()
                .line_break();
        }

        xml.start_complex_open_tag("trait")
            .attr("name", "owner")
            .attr("value", test.owner)
            .finish_self_closing_complex_tag()
            .line_break();
        xml.close_tag("traits").line_break();

        if (result_string == "Fail")
        {
            xml.open_tag("failure")
                .open_tag("message")
                .cdata(to_string_locale_invariant(test.result))
                .close_tag("message")
                .close_tag("failure")
                .line_break();
        }
        else if (result_string == "Skip")
        {
            xml.open_tag("reason").cdata(to_string_locale_invariant(test.result)).close_tag("reason").line_break();
        }
        else
        {
            Checks::check_exit(VCPKG_LINE_INFO, result_string == "Pass");
        }
        xml.close_tag("test").line_break();
    }

}

XunitWriter::XunitWriter() = default;
XunitWriter::~XunitWriter() = default;

void XunitWriter::add_test_results(const PackageSpec& spec,
                                   BuildResult build_result,
                                   const ElapsedTime& elapsed_time,
                                   const std::chrono::system_clock::time_point& start_time,
                                   const std::string& abi_tag,
                                   const std::vector<std::string>& features)
{
    m_tests[spec.name()].push_back(
        {spec.to_string(),
         Strings::concat(spec.name(), '[', Strings::join(",", features), "]:", spec.triplet()),
         spec.triplet().to_string(),
         build_result,
         elapsed_time,
         start_time,
         abi_tag,
         features});
}

std::string XunitWriter::build_xml(Triplet controlling_triplet)
{
    XmlSerializer xml;
    xml.emit_declaration();
    xml.open_tag("assemblies").line_break();
    for (const auto& test_group : m_tests)
    {
        const auto& port_name = test_group.first;
        const auto& port_results = test_group.second;

        ElapsedTime elapsed_sum{};
        for (auto&& port_result : port_results)
        {
            elapsed_sum += port_result.time;
        }

        const auto elapsed_seconds = elapsed_sum.as<std::chrono::seconds>().count();

        auto earliest_start_time =
            std::min_element(port_results.begin(), port_results.end(), [](const XunitTest& lhs, const XunitTest& rhs) {
                return lhs.start_time < rhs.start_time;
            })->start_time;

        const auto as_time_t = std::chrono::system_clock::to_time_t(earliest_start_time);
        const auto as_tm = to_utc_time(as_time_t).value_or_exit(VCPKG_LINE_INFO);
        char run_date_time[80];
        strftime(run_date_time, sizeof(run_date_time), "%Y-%m-%d%H:%M:%S", &as_tm);

        StringView run_date{run_date_time, 10};
        StringView run_time{run_date_time + 10, 8};

        xml.start_complex_open_tag("assembly")
            .attr("name", port_name)
            .attr("run-date", run_date)
            .attr("run-time", run_time)
            .attr("time", elapsed_seconds)
            .finish_complex_open_tag()
            .line_break();
        xml.start_complex_open_tag("collection")
            .attr("name", controlling_triplet)
            .attr("time", elapsed_seconds)
            .finish_complex_open_tag()
            .line_break();
        for (const auto& port_result : port_results)
        {
            xml_test(xml, port_result);
        }
        xml.close_tag("collection").line_break();
        xml.close_tag("assembly").line_break();
    }

    xml.close_tag("assemblies").line_break();
    return std::move(xml.buf);
}
