#pragma once

#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/triplet.h>

#include <vcpkg/base/chrono.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace vcpkg
{
    struct XunitTest;

    // https://xunit.net/docs/format-xml-v2
    struct XunitWriter
    {
    public:
        // Out of line con/destructor avoids exposing XunitTest
        XunitWriter();
        ~XunitWriter() noexcept;
        void add_test_results(const PackageSpec& spec,
                              BuildResult build_result,
                              const ElapsedTime& elapsed_time,
                              const std::chrono::system_clock::time_point& start_time,
                              const std::string& abi_tag,
                              const std::vector<std::string>& features);

        std::string build_xml(Triplet controlling_triplet) const;

    private:
        std::map<std::string, std::vector<XunitTest>> m_tests;
    };
}
