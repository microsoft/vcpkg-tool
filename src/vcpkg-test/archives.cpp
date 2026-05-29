#include <vcpkg/base/system-headers.h>

#include <vcpkg-test/util.h>

#include <vcpkg/archives.h>

#include <string>

TEST_CASE ("Testing guess_extraction_type", "[z-extract]")
{
    using namespace vcpkg;
    REQUIRE(guess_extraction_type(Path("path/to/archive.nupkg")) == ExtractionType::Nupkg);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.zip")) == ExtractionType::Zip);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.7z")) == ExtractionType::SevenZip);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.gz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.bz2")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.tgz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.xz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.exe")) == ExtractionType::Exe);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.unknown")) == ExtractionType::Unknown);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.7z.exe")) == ExtractionType::SelfExtracting7z);
}

// Regression test for microsoft/vcpkg#50051: concurrent vcpkg processes must not collide
// on the same `.partial` extraction directory. The per-PID suffix is what keeps them apart.
TEST_CASE ("extraction_partial_path is per-process", "[z-extract]")
{
    using namespace vcpkg;
#if defined(_WIN32)
    const auto pid = std::to_string(GetCurrentProcessId());
#else
    const auto pid = std::to_string(getpid());
#endif

    const auto partial = extraction_partial_path(Path("/some/tools/ninja-1.13.2-linux"));
    REQUIRE(partial.native() == "/some/tools/ninja-1.13.2-linux.partial." + pid);
}
