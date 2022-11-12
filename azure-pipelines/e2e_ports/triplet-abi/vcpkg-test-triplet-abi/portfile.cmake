set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

file(STRINGS "${CURRENT_BUILDTREES_DIR}/vcpkg-test-triplet-abi.vcpkg_abi_info.txt" actual_abi_lines)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/expected-abi.txt" expected_abi_lines)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/unexpected-abi.txt" unexpected_abi_lines)

foreach(expected_line IN LISTS expected_abi_lines)
    if(NOT "${expected_line}" IN_LIST actual_abi_lines)
        message(FATAL_ERROR "Expected ABI: '${expected_line}' \n not within actual ABI: '${actual_abi_lines}'")
    endif()
endforeach()

foreach(unexpected_abi_line IN LISTS unexpected_abi_lines)
    if("${unexpected_abi_line}" IN_LIST actual_abi_lines)
        message(FATAL_ERROR "Unexpected ABI: '${expected_line}' \n found within actual ABI: '${actual_abi_lines}'")
    endif()
endforeach()