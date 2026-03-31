option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
# A cache variable cannot be used it here because it will break contributors' builds on fmt update.
if("$CACHE{VCPKG_FMT_URL}" MATCHES "^https://github.com/fmtlib/fmt/archive/refs/tags")
    unset(VCPKG_FMT_URL CACHE) # Fix upgrade
endif()
if(NOT VCPKG_FMT_URL)
    set(VCPKG_FMT_URL "https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(OLD_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(SKIP_WARNINGS OFF)
if(MSVC AND VCPKG_DEVELOPMENT_WARNINGS AND NOT (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang") AND NOT (CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang"))
    set(SKIP_WARNINGS ON)
    # fmt\base.h(930): warning C4834: discarding return value of function with [[nodiscard]] attribute
    string(APPEND CMAKE_CXX_FLAGS " /wd4834")
    # fmt\base.h(930): warning C6031: Return value ignored: 'std::isalpha'.
    string(APPEND CMAKE_CXX_FLAGS " /wd6031")
    # base.h(1711) : warning C6294: Ill-defined for-loop.  Loop body not executed.
    string(APPEND CMAKE_CXX_FLAGS " /wd6294")
    # fmt\format.h(1058) : warning C6240: (<expression> && <non-zero constant>) always evaluates to the result of <expression>:  Did you intend to use the bitwise-and (`&`) 
    string(APPEND CMAKE_CXX_FLAGS " /wd6240")
    # This one is guarded by an assert
    # fmt\os.h(377): warning C6326: Potential comparison of a constant with another constant.
    string(APPEND CMAKE_CXX_FLAGS " /wd6326")
endif()

include(FetchContent)
FetchContent_Declare(
    fmt
    URL "${VCPKG_FMT_URL}"
    URL_HASH "SHA512=f0da82c545b01692e9fd30fdfb613dbb8dd9716983dcd0ff19ac2a8d36f74beb5540ef38072fdecc1e34191b3682a8542ecbf3a61ef287dbba0a2679d4e023f2"
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(fmt)
endif()

if(SKIP_WARNINGS)
    set(CMAKE_CXX_FLAGS "${OLD_CXX_FLAGS}")
endif()
