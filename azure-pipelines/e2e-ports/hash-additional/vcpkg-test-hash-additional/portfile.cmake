set(ABI_FILE "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}.vcpkg_abi_info.txt")
file(STRINGS "${ABI_FILE}" lines)
list(FILTER lines INCLUDE REGEX "additional_file_.+")

if(lines STREQUAL "additional_file_0 61ba0c7fc1f696e28c1b7aa9460980a571025ff8c97bb90a57e990463aa25660")
    message(STATUS "Test successful!")
else()
    list(JOIN lines "\n    " lines)
    message(FATAL_ERROR "Expected exactly one expected additional file in ${ABI_FILE} but got:\n    ${lines}")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
