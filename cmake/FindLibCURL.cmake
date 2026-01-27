if (WIN32 OR APPLE)
    set(VCPKG_LIBCURL "SYSTEM" CACHE STRING "Select libcurl provider (SYSTEM|DLSYM)")
else()
    set(VCPKG_LIBCURL "DLSYM" CACHE STRING "Select libcurl provider (SYSTEM|DLSYM)")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if (VCPKG_LIBCURL STREQUAL "SYSTEM")
    find_package(CURL REQUIRED)
    return()
elseif (NOT VCPKG_LIBCURL STREQUAL "DLSYM")
    message(FATAL_ERROR "Unsupported VCPKG_LIBCURL value '${VCPKG_LIBCURL}'. Expected SYSTEM or DLSYM.")
endif()

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
#
# We're using curl-7_29_0 headers here because that's what comes with RHEL 7 (and inherited by
# similarly ancient Oracle Linux 7)
if(NOT VCPKG_LIBCURL_URL)
    set(VCPKG_LIBCURL_URL "https://curl.se/download/archeology/curl-7.29.0.tar.gz")
endif()

if(NOT TARGET CURL::libcurl)
include(FetchContent)
    FetchContent_Declare(
        LibCURLHeaders
        URL "${VCPKG_LIBCURL_URL}"
        URL_HASH "SHA512=08bafd09fa6d14362a426932fed8528c13133895477d8134c829e085637956d66d6be5a791057c1c04da04af6baa6496a6d59e00abf9ca6be5d29e798718b9bc"
    )

    FetchContent_GetProperties(LibCURLHeaders)
    # This dance is done rather than `FetchContent_MakeAvailable` because we only want to download
    # curl's headers for use with dlopen/dlsym rather than building curl.
    if(NOT LibCURLHeaders_POPULATED)
        FetchContent_Populate(LibCURLHeaders)
    endif()

    add_library(CURL::libcurl INTERFACE IMPORTED)

    set_target_properties(CURL::libcurl PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${libcurlheaders_SOURCE_DIR}/include"
        INTERFACE_COMPILE_DEFINITIONS "VCPKG_LIBCURL_DLSYM=1"
    )
endif()
