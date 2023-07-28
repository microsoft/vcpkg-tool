#include <catch2/catch.hpp>
#include <vcpkg/archives.h>


TEST_CASE ("Testing guess_extraction_type", "[z-extract]")
{
    using namespace vcpkg;
    REQUIRE(guess_extraction_type(Path("path/to/archive.nupkg")) == ExtractionType::Nupkg);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.msi")) == ExtractionType::Msi);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.zip")) == ExtractionType::Zip);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.7z")) == ExtractionType::Zip);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.gz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.bz2")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.tgz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.xz")) == ExtractionType::Tar);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.exe")) == ExtractionType::Exe);
    REQUIRE(guess_extraction_type(Path("/path/to/archive.unknown")) == ExtractionType::Unknown);

}
