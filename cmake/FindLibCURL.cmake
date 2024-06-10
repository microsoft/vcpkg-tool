option(VCPKG_DEPENDENCY_EXTERNAL_LIBCURL "Use an external version of the libcurl library" OFF)

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
if(NOT VCPKG_LIBCURL_URL)
    set(VCPKG_LIBCURL_URL "https://github.com/curl/curl/archive/refs/tags/curl-8_8_0.tar.gz")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(
    LibCURL
    URL "${VCPKG_LIBCURL_URL}"
    URL_HASH "SHA512=e66cbf9bd3ae7b9b031475210b80b883b6a133042fbbc7cf2413f399d1b38aa54ab7322626abd3c6f1af56e0d540221f618aa903bd6b463ac8324f2c4e92dfa8"
)

if(NOT LibCURL_FIND_REQUIRED)
    message(FATAL_ERROR "LibCURL must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(CURL REQUIRED)
else()
    block()
        set(BUILD_SHARED_LIBS OFF)
        set(BUILD_STATIC_LIBS ON)
        set(BUILD_CURL_EXE OFF)
        set(CURL_DISABLE_INSTALL OFF)
        #set(CURL_STATIC_CRT ON)
        set(ENABLE_UNICODE ON)
        set(CURL_ENABLE_EXPORT_TARGET OFF)
        set(BUILD_LIBCURL_DOCS  OFF)
        set(BUILD_MISC_DOCS OFF)
        set(ENABLE_CURL_MANUAL OFF)
        set(CMAKE_DISABLE_FIND_PACKAGE_Perl ON)
        set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
        set(CMAKE_DISABLE_FIND_PACKAGE_LibPSL ON)
        set(CMAKE_DISABLE_FIND_PACKAGE_LibSSH2 ON)
        if(MSVC) # This is in block() so no need to backup the variables
            string(APPEND CMAKE_C_FLAGS " /wd6101")
            string(APPEND CMAKE_C_FLAGS " /wd6011")
            string(APPEND CMAKE_C_FLAGS " /wd6054")
            string(APPEND CMAKE_C_FLAGS " /wd6240")
            string(APPEND CMAKE_C_FLAGS " /wd6239")
            string(APPEND CMAKE_C_FLAGS " /wd6323")
            string(APPEND CMAKE_C_FLAGS " /wd6387")
            string(APPEND CMAKE_C_FLAGS " /wd28182")
            string(APPEND CMAKE_C_FLAGS " /wd28183")
            string(APPEND CMAKE_C_FLAGS " /wd28251")      
        endif()
        FetchContent_MakeAvailable(LibCURL)        
    endblock()
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl INTERFACE)
        target_link_libraries(CURL::libcurl PUBLIC libcurl_static)
    endif()
endif()
