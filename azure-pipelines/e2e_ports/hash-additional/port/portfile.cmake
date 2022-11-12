file(STRINGS "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}.vcpkg_abi_info.txt" lines)
list(GET lines 0 first_line)

set(expected "additional_abi_file.txt 61ba0c7fc1f696e28c1b7aa9460980a571025ff8c97bb90a57e990463aa25660")

if(first_line STREQUAL "${expected}")
    message(STATUS "Test succesful!")
else()
    message(FATAL_ERROR "First line in abi info is not the additional file to be hashed but:\n first_line: '${first_line}'\n expected: '${expected}' ")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
