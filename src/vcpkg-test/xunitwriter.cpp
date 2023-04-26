#include <catch2/catch.hpp>

#include <vcpkg/commands.build.h>
#include <vcpkg/xunitwriter.h>

#include <ostream>
#include <utility>

using namespace vcpkg;

TEST_CASE ("Simple XunitWriter", "[xunitwriter]")
{
    XunitWriter x;
    time_t time = {0};
    Triplet t = Triplet::from_canonical_name("triplet");
    PackageSpec spec("name", t);
    x.add_test_results(spec, BuildResult::BUILD_FAILED, {}, std::chrono::system_clock::from_time_t(time), "", {});
    CHECK(x.build_xml(t) == R"(<?xml version="1.0" encoding="utf-8"?><assemblies>
  <assembly name="name" run-date="1970-01-01" run-time="00:00:00" time="0">
    <collection name="triplet" time="0">
      <test name="name:triplet" method="name[]:triplet" time="0" result="Fail">
        <traits>
          <trait name="owner" value="triplet"/>
        </traits>
        <failure><message><![CDATA[BUILD_FAILED]]></message></failure>
      </test>
    </collection>
  </assembly>
</assemblies>
)");
}

TEST_CASE ("XunitWriter Two", "[xunitwriter]")
{
    XunitWriter x;
    time_t time = {0};
    Triplet t = Triplet::from_canonical_name("triplet");
    Triplet t2 = Triplet::from_canonical_name("triplet2");
    Triplet t3 = Triplet::from_canonical_name("triplet3");
    PackageSpec spec("name", t);
    PackageSpec spec2("name", t2);
    PackageSpec spec3("other", t2);
    x.add_test_results(spec, BuildResult::SUCCEEDED, {}, std::chrono::system_clock::from_time_t(time), "abihash", {});
    x.add_test_results(
        spec2, BuildResult::POST_BUILD_CHECKS_FAILED, {}, std::chrono::system_clock::from_time_t(time), "", {});
    x.add_test_results(
        spec3, BuildResult::SUCCEEDED, {}, std::chrono::system_clock::from_time_t(time), "", {"core", "feature"});
    CHECK(x.build_xml(t3) == R"(<?xml version="1.0" encoding="utf-8"?><assemblies>
  <assembly name="name" run-date="1970-01-01" run-time="00:00:00" time="0">
    <collection name="triplet3" time="0">
      <test name="name:triplet" method="name[]:triplet" time="0" result="Pass">
        <traits>
          <trait name="abi_tag" value="abihash"/>
          <trait name="owner" value="triplet"/>
        </traits>
      </test>
      <test name="name:triplet2" method="name[]:triplet2" time="0" result="Fail">
        <traits>
          <trait name="owner" value="triplet2"/>
        </traits>
        <failure><message><![CDATA[POST_BUILD_CHECKS_FAILED]]></message></failure>
      </test>
    </collection>
  </assembly>
  <assembly name="other" run-date="1970-01-01" run-time="00:00:00" time="0">
    <collection name="triplet3" time="0">
      <test name="other:triplet2" method="other[core,feature]:triplet2" time="0" result="Pass">
        <traits>
          <trait name="features" value="core, feature"/>
          <trait name="owner" value="triplet2"/>
        </traits>
      </test>
    </collection>
  </assembly>
</assemblies>
)");
}
