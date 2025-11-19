# This option exists to allow contributors to use an external libcurl installation,
# but it should not be used in official builds.
option(VCPKG_DEPENDENCY_EXTERNAL_LIBCURL "Use an external version of the libcurl library" OFF)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if (VCPKG_DEPENDENCY_EXTERNAL_LIBCURL)
    find_package(CURL REQUIRED)
    return()
endif()

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
if(NOT VCPKG_LIBCURL_URL)
    set(VCPKG_LIBCURL_URL "https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.gz")
endif()

include(FetchContent)
FetchContent_Declare(
    LibCURL
    URL "${VCPKG_LIBCURL_URL}"
    URL_HASH "SHA512=88ab4b7aac12b26a6ad32fb0e1a9675288a45894438cb031102ef5d4ab6b33c2bc99cae0c70b71bdfa12eb49762827e2490555114c5eb4a6876b95e1f2a4eb74"
)

if(NOT LibCURL_FIND_REQUIRED)
    message(FATAL_ERROR "LibCURL must be REQUIRED")
endif()

# This is in function() so no need to backup the variables
function(get_libcurl)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS  OFF)
    set(BUILD_MISC_DOCS OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(CURL_ENABLE_EXPORT_TARGET OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_LIBPSL OFF)
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
        set(CURL_USE_OPENSSL OFF)
    elseif(APPLE)
        set(CURL_USE_SECTRANSP ON)
        set(CURL_USE_OPENSSL OFF)
    elseif(UNIX)
        set(CURL_USE_OPENSSL ON)
        set(CURL_USE_SCHANNEL OFF)
        set(CURL_USE_SECTRANSP OFF)
        set(CURL_ENABLE_SSL ON)
    endif()

    # vcpkg tool only needs HTTP(S) downloads; disable other protocols and
    # high-surface features in the embedded libcurl to reduce attack surface.
    # Keep FILE support in case file:// URLs are ever needed.
    set(CURL_DISABLE_FTP ON)
    set(CURL_DISABLE_GOPHER ON)
    set(CURL_DISABLE_IMAP ON)
    set(CURL_DISABLE_IPFS ON)
    set(CURL_DISABLE_LDAP ON)
    set(CURL_DISABLE_LDAPS ON)
    set(CURL_DISABLE_MQTT ON)
    set(CURL_DISABLE_POP3 ON)
    set(CURL_DISABLE_RTSP ON)
    set(CURL_DISABLE_SMB ON)
    set(CURL_DISABLE_SMTP ON)
    set(CURL_DISABLE_TELNET ON)
    set(CURL_DISABLE_TFTP ON)
    set(CURL_DISABLE_WEBSOCKETS ON)

    # Extra HTTP-related features that vcpkg does not rely on.
    set(CURL_DISABLE_ALTSVC ON)
    set(CURL_DISABLE_HSTS ON)
    set(CURL_DISABLE_DOH ON)
    set(CURL_DISABLE_AWS ON)
    set(CURL_DISABLE_HEADERS_API ON)
    set(CURL_DISABLE_GETOPTIONS ON)
    set(CURL_DISABLE_LIBCURL_OPTION ON)
    set(CURL_DISABLE_NETRC ON)
    set(CURL_DISABLE_PROGRESS_METER ON)
    set(CURL_DISABLE_SHUFFLE_DNS ON)
    set(CURL_DISABLE_SOCKETPAIR ON)
    set(CURL_DISABLE_VERBOSE_STRINGS ON)

    set(ENABLE_CURL_MANUAL OFF)
    set(ENABLE_UNICODE ON)
    set(PICKY_COMPILER OFF)
    set(USE_NGHTTP2 OFF)
    set(USE_LIBIDN2 OFF)
    set(CMAKE_DISABLE_FIND_PACKAGE_Perl ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_LibPSL ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_LibSSH2 ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_Brotli ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_Zstd ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_NGHTTP2 ON)
    set(CMAKE_DISABLE_FIND_PACKAGE_Libidn2 ON)
    if(MSVC)
        string(APPEND CMAKE_C_FLAGS " /wd6101")
        string(APPEND CMAKE_C_FLAGS " /wd6011")
        string(APPEND CMAKE_C_FLAGS " /wd6054")
        string(APPEND CMAKE_C_FLAGS " /wd6287")
        string(APPEND CMAKE_C_FLAGS " /wd6323")
        string(APPEND CMAKE_C_FLAGS " /wd6385")
        string(APPEND CMAKE_C_FLAGS " /wd6387")
        string(APPEND CMAKE_C_FLAGS " /wd28182")
        string(APPEND CMAKE_C_FLAGS " /wd28251")
        string(APPEND CMAKE_C_FLAGS " /wd28301")
    else()
        string(APPEND CMAKE_C_FLAGS " -Wno-error")
    endif()
    FetchContent_MakeAvailable(LibCURL)
endfunction()

get_libcurl()

if(NOT TARGET CURL::libcurl)
    if(TARGET libcurl_static)
        add_library(CURL::libcurl ALIAS libcurl_static)
        target_compile_definitions(libcurl_static INTERFACE CURL_STATICLIB)
    elseif(TARGET libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
        if(NOT BUILD_SHARED_LIBS)
            target_compile_definitions(libcurl INTERFACE CURL_STATICLIB)
        endif()
    else()
        message(FATAL_ERROR "After FetchContent_MakeAvailable(LibCURL) no suitable curl target (libcurl or libcurl_static) was found.")
    endif()
endif()
