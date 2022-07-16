option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)

set(VCPKG_FMT_VERSION "9.0.0" CACHE STRING "Required fmt release version.")
set(VCPKG_FMT_HASH "f9612a53c93654753572ac038e52c683f3485691493750d5c2fdb48f3a769e181bfeab8035041cae02bf14cd67df30ec3c5614d7db913f85699cd9da8072bdf8" CACHE STRING "SHA512 hash of the fmt release tarball.")
set(VCPKG_FMT_URL "https://github.com/fmtlib/fmt/archive/refs/tags/${VCPKG_FMT_VERSION}.tar.gz" CACHE STRING "URL to the fmt release tarball to use.")

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
