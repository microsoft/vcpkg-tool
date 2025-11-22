#include <vcpkg/base/chrono.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/xunitwriter.h>

namespace vcpkg
{
    struct XunitTest
    {
        std::string name;
        std::string method;
        std::string owner;
        CiResult ci_result;
    };
}

namespace
{
    using namespace vcpkg;
    void xml_test(XmlSerializer& xml, const XunitTest& test)
    {
        StringLiteral result_string = "";
        switch (test.ci_result.code)
        {
            case BuildResult::Succeeded: result_string = "Pass"; break;
            case BuildResult::BuildFailed:
            case BuildResult::PostBuildChecksFailed:
            case BuildResult::FileConflicts: result_string = "Fail"; break;
            case BuildResult::CascadedDueToMissingDependencies:
            case BuildResult::Excluded:
            case BuildResult::ExcludedByParent:
            case BuildResult::ExcludedByDryRun:
            case BuildResult::Unsupported:
            case BuildResult::Cached: result_string = "Skip"; break;
            case BuildResult::CacheMissing:
            case BuildResult::Downloaded:
            case BuildResult::Removed:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        long long time_seconds = 0ll;
        if (const auto* build = test.ci_result.build.get())
        {
            time_seconds = build->timing.as<std::chrono::seconds>().count();
        }

        xml.start_complex_open_tag("test")
            .attr("name", test.name)
            .attr("method", test.method)
            .attr("time", fmt::format("{}", time_seconds))
            .attr("result", result_string)
            .finish_complex_open_tag()
            .line_break();

        xml.open_tag("traits").line_break();
        if (const auto* build = test.ci_result.build.get())
        {
            xml.start_complex_open_tag("trait")
                .attr("name", "abi_tag")
                .attr("value", build->package_abi)
                .finish_self_closing_complex_tag()
                .line_break();

            xml.start_complex_open_tag("trait")
                .attr("name", "features")
                .attr("value", Strings::join(",", build->feature_list))
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
                .cdata(to_string_locale_invariant(test.ci_result.code))
                .close_tag("message")
                .close_tag("failure")
                .line_break();
        }
        else if (result_string == "Skip")
        {
            xml.open_tag("reason")
                .cdata(to_string_locale_invariant(test.ci_result.code))
                .close_tag("reason")
                .line_break();
        }
        else
        {
            Checks::check_exit(VCPKG_LINE_INFO, result_string == "Pass");
        }
        xml.close_tag("test").line_break();
    }
} // unnamed namespace

namespace vcpkg
{
    std::string CiResult::to_string() const { return adapt_to_string(*this); }
    void CiResult::to_string(std::string& out_str) const
    {
        out_str.append(vcpkg::to_string(code).data());
        if (auto b = build.get())
        {
            out_str.append(": ");
            b->timing.to_string(out_str);
        }
    }

    XunitWriter::XunitWriter() = default;
    XunitWriter::~XunitWriter() = default;

    void XunitWriter::add_test_results(const PackageSpec& spec, const CiResult& result)
    {
        const auto& name = spec.name();
        std::string method = name;
        if (auto* built = result.build.get())
        {
            fmt::format_to(std::back_inserter(method), "[{}]", Strings::join(",", built->feature_list));
        }

        fmt::format_to(std::back_inserter(method), ":{}", spec.triplet());

        m_tests[spec.name()].push_back({spec.to_string(), std::move(method), spec.triplet().to_string(), result});
    }

    std::string XunitWriter::build_xml(Triplet controlling_triplet) const
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
                if (const auto* build = port_result.ci_result.build.get())
                {
                    elapsed_sum += build->timing;
                }
            }

            const auto elapsed_seconds = fmt::format("{}", elapsed_sum.as<std::chrono::seconds>().count());

            std::chrono::system_clock::time_point earliest_start_time = std::chrono::system_clock::time_point::max();
            for (auto&& port_result : port_results)
            {
                if (const auto* build = port_result.ci_result.build.get())
                {
                    if (build->start_time < earliest_start_time)
                    {
                        earliest_start_time = build->start_time;
                    }
                }
            }

            time_t as_time_t;
            if (earliest_start_time == std::chrono::system_clock::time_point::max())
            {
                as_time_t = 0;
            }
            else
            {
                as_time_t = std::chrono::system_clock::to_time_t(earliest_start_time);
            }

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
}
