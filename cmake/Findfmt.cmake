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
    set(VCPKG_FMT_URL "https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.tar.gz")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(
    fmt
    URL "${VCPKG_FMT_URL}"
    URL_HASH "SHA512=46974efd36e613477351aa357c451cee434da797c2a505f9f86d73e394dcb35dc2dc0cda66abb98c023e8f24deac9d8e3ee6f9f6c0971cc4c00e37c34aa7f15f"
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(fmt)
endif()
