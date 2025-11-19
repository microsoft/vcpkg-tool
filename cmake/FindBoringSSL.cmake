if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
if(NOT VCPKG_BORINGSSL_URL)
    set(VCPKG_BORINGSSL_URL "https://github.com/google/boringssl/releases/download/0.20251110.0/boringssl-0.20251110.0.tar.gz")
endif()

include(FetchContent)
find_package(Git REQUIRED)
FetchContent_Declare(
    BoringSSL
    URL "${VCPKG_BORINGSSL_URL}"
    URL_HASH "SHA512=b017d3ae05a7491374c6f6b249220c8dfa6955748084e9701afdfc58c1dc9c9ff25f735510c3ca37f8ce61c9f72f22cf94a7a525ca9f6e1ad4ab3b93652f525b"
    PATCH_COMMAND "${GIT_EXECUTABLE}" "--work-tree=." apply "${CMAKE_CURRENT_LIST_DIR}/boringssl_warnings.patch"
)

if(NOT BoringSSL_FIND_REQUIRED)
    message(FATAL_ERROR "BoringSSL must be REQUIRED")
endif()

FetchContent_MakeAvailable(BoringSSL)

if(NOT TARGET BoringSSL::ssl)
    if(TARGET ssl)
        add_library(BoringSSL::ssl ALIAS ssl)
    endif()
endif()

if(NOT TARGET BoringSSL::crypto)
    if(TARGET crypto)
        add_library(BoringSSL::crypto ALIAS crypto)
    endif()
endif()
