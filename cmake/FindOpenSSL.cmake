if(UNIX AND NOT APPLE)
    find_package(BoringSSL REQUIRED)

    if(NOT TARGET OpenSSL::SSL)
        if(TARGET ssl)
            add_library(OpenSSL::SSL ALIAS ssl)
        else()
             message(FATAL_ERROR "Target 'ssl' not found from BoringSSL")
        endif()
    endif()
    if(NOT TARGET OpenSSL::Crypto)
        if(TARGET crypto)
            add_library(OpenSSL::Crypto ALIAS crypto)
        else()
             message(FATAL_ERROR "Target 'crypto' not found from BoringSSL")
        endif()
    endif()

    FetchContent_GetProperties(BoringSSL)
    set(OPENSSL_INCLUDE_DIR "${boringssl_SOURCE_DIR}/include")
    set(OPENSSL_LIBRARIES BoringSSL::ssl BoringSSL::crypto)
    set(OPENSSL_FOUND TRUE)
    set(OPENSSL_VERSION "1.1.1")

    # Pre-fill Curl's checks to avoid linking errors during configuration
    # because BoringSSL is being built in the same project and not yet
    # available as traditional OpenSSL imported targets inside
    # try_compile() projects.
    set(HAVE_BORINGSSL TRUE CACHE INTERNAL "")
    set(HAVE_AWSLC FALSE CACHE INTERNAL "")
    set(HAVE_LIBRESSL FALSE CACHE INTERNAL "")

    # Assume BoringSSL has standard functions but maybe not
    # deprecated/obscure ones. Predefining these avoids curl running
    # its own feature-detection checks that rely on try_compile with
    # imported targets, which does not work reliably in this
    # superbuild-style configuration.
    set(HAVE_SSL_SET0_WBIO TRUE CACHE INTERNAL "")
    set(HAVE_OPENSSL_SRP FALSE CACHE INTERNAL "")
    set(HAVE_DES_ECB_ENCRYPT FALSE CACHE INTERNAL "") # BoringSSL might not export this

    # QUIC related
    set(HAVE_SSL_SET_QUIC_TLS_CBS FALSE CACHE INTERNAL "")
    set(HAVE_SSL_SET_QUIC_USE_LEGACY_CODEPOINT FALSE CACHE INTERNAL "")

    return()
endif()

if(EXISTS "${CMAKE_ROOT}/Modules/FindOpenSSL.cmake")
    include("${CMAKE_ROOT}/Modules/FindOpenSSL.cmake")
else()
    message(FATAL_ERROR "Could not find standard FindOpenSSL.cmake")
endif()
