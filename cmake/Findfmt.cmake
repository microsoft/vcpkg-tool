option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)
option(VCPKG_DEPENDENCY_TERRAPIN_FMT "Use a version of the fmt library downloaded from terrapin (microsoft-internal)" OFF)

if(VCPKG_DEPENDENCY_EXTERNAL_FMT AND VCPKG_DEPENDENCY_TERRAPIN_FMT)
    message(FATAL_ERROR "VCPKG_DEPENDENCY_EXTERNAL_FMT and VCPKG_DEPENDENCY_TERRAPIN_FMT are mutually exclusive options")
endif()

set(git_tag b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9)
set(tar_sha512 805424979dbed28ba0a48f69928a14d122de50f21dcadb97f852dcc415ab8a7a30fcf2eb90c06f006c54cbea00fcfe449d340cbb40e6a0454fffbc009fbe25e5)

include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG "${git_tag}" # 8.1.1
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
else()
    if(VCPKG_DEPENDENCY_TERRAPIN_FMT)
        set(output_tar "${CMAKE_BINARY_DIR}/fmt-${tar_sha512}.tar.gz")
        file(DOWNLOAD
            "https://vcpkg.storage.devpackages.microsoft.io/artifacts/${tar_sha512}"
            "${output_tar}"
            EXPECTED_HASH "SHA512=${tar_sha512}"
        )
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${output_tar}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        )

        set(FETCHCONTENT_FULLY_DISCONNECTED ON)
        set(FETCHCONTENT_SOURCE_DIR_FMT "${CMAKE_BINARY_DIR}/fmt-${git_tag}")
    endif()
    FetchContent_MakeAvailable(fmt)
endif()
