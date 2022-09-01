option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)

set(VCPKG_FMT_VERSION "9.1.0" CACHE STRING "Required fmt release version.")
set(VCPKG_FMT_HASH "a18442042722dd48e20714ec034a12fcc0576c9af7be5188586970e2edf47529825bdc99af366b1d5891630c8dbf6f63bfa9f012e77ab3d3ed80d1a118e3b2be" STRING "SHA512 hash of the fmt release tarball.")
set(VCPKG_FMT_URL "https://github.com/fmtlib/fmt/archive/refs/tags/${VCPKG_FMT_VERSION}.tar.gz" STRING "URL to the fmt release tarball to use.")


include(FetchContent)
FetchContent_Declare(
    fmt
    URL "${VCPKG_FMT_URL}"
    URL_HASH "SHA512=${VCPKG_FMT_HASH}"
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt ${VCPKG_FMT_VERSION} CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(fmt)
endif()
