set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

if(NOT DEFINED ENV{VCPKG_ENV_TEST} AND NOT "$ENV{VCPKG_ENV_TEST}" STREQUAL "TRUE")
    message(FATAL_ERROR "ENV{VCPKG_ENV_TEST} not set or has wrong value of '$ENV{VCPKG_ENV_TEST}' (expected TRUE)")
endif()

if(NOT DEFINED ENV{VCPKG_ENV_TEST2} AND NOT "$ENV{VCPKG_ENV_TEST2}" STREQUAL "MORE_TESTING")
    message(FATAL_ERROR "ENV{VCPKG_ENV_TEST2} not set or has wrong value of '$ENV{VCPKG_ENV_TEST2}' (expected MORE_TESTING)")
endif()

file(READ "${CURRENT_BUILDTREES_DIR}/../env-script.log" contents)
file(REMOVE "${CURRENT_BUILDTREES_DIR}/../env-script.log")

if(NOT contents STREQUAL "DOWNLOADS:${DOWNLOADS}")
    message(FATAL_ERROR "contents (${contents}) of 'env-script.log' are not equal to 'DOWNLOADS:${DOWNLOADS}'")
endif()
