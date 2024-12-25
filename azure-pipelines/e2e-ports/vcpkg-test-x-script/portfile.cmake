# This port is for testing that x-script works; see test-script-asset-cache.c
vcpkg_download_distfile(
    SOURCE_PATH
    URLS https://example.com/hello-world.txt
    SHA512 09E1E2A84C92B56C8280F4A1203C7CFFD61B162CFE987278D4D6BE9AFBF38C0E8934CDADF83751F4E99D111352BFFEFC958E5A4852C8A7A29C95742CE59288A8
    FILENAME hello-world.txt
)

file(READ "${SOURCE_PATH}" CONTENTS)
if (NOT CONTENTS STREQUAL "Hello, world!\n")
    message(FATAL_ERROR "Downloaded file has incorrect contents: ${CONTENTS}")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
