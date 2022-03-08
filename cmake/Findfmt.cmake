option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)
set(VCPKG_FMT_URL "https://github.com/fmtlib/fmt/archive/refs/tags/8.1.1.tar.gz" CACHE STRING "URL to the fmt release tarball to use.")

include(FetchContent)
FetchContent_Declare(
    fmt
    URL "${VCPKG_FMT_URL}"
    URL_HASH SHA512=794a47d7cb352a2a9f2c050a60a46b002e4157e5ad23e15a5afc668e852b1e1847aeee3cda79e266c789ff79310d792060c94976ceef6352e322d60b94e23189
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(fmt)
endif()
