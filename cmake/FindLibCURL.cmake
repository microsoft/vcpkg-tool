if (WIN32 OR APPLE)
    set(VCPKG_LIBCURL_DLSYM_DEFAULT "OFF")
else()
    set(VCPKG_LIBCURL_DLSYM_DEFAULT "ON")
endif()

option(VCPKG_LIBCURL_DLSYM "Select libcurl provider (SYSTEM|DLSYM)" "${VCPKG_LIBCURL_DLSYM_DEFAULT}")
option(VCPKG_LIBCURL_DLSYM_UPDATED_HEADERS "Use more recent libcurl headers when using DLSYM than 7.29.0" OFF)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if (NOT VCPKG_LIBCURL_DLSYM)
    find_package(CURL REQUIRED)
    return()
endif()

# The URI option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
if (VCPKG_LIBCURL_DLSYM_UPDATED_HEADERS)
    # curl-7_55_1 for https://daniel.haxx.se/blog/2017/06/15/target-independent-libcurl-headers/
    # (mainly for arm64 support)
    set(VCPKG_LIBCURL_HASH 4b0bf36a978b8b5ba66aecedbc2ae8ae9230da63ba5b80f9553d96671e013ccd679ee9cc10946c50b94d640858d74f3ec5d4e198c6b9f8842c941986d275cf7a)
    if(NOT VCPKG_LIBCURL_URL)
        set(VCPKG_LIBCURL_URL "https://curl.se/download/curl-7.55.1.tar.gz")
    endif()
else()
    # curl-7_29_0 headers here because that's what comes with RHEL 7 (and inherited by similarly ancient Oracle Linux 7)
    set(VCPKG_LIBCURL_HASH 08bafd09fa6d14362a426932fed8528c13133895477d8134c829e085637956d66d6be5a791057c1c04da04af6baa6496a6d59e00abf9ca6be5d29e798718b9bc)
    if(NOT VCPKG_LIBCURL_URL)
        set(VCPKG_LIBCURL_URL "https://curl.se/download/archeology/curl-7.29.0.tar.gz")
    endif()
endif()

if(NOT TARGET CURL::libcurl)
include(FetchContent)
    FetchContent_Declare(
        LibCURLHeaders
        URL "${VCPKG_LIBCURL_URL}"
        URL_HASH "SHA512=${VCPKG_LIBCURL_HASH}"
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
