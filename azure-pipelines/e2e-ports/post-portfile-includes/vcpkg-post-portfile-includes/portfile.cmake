file(STRINGS "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}.vcpkg_abi_info.txt" lines)
list(FILTER lines INCLUDE REGEX "post_portfile_include_(0|1)")
list(GET lines 0 first_line)
list(GET lines 1 second_line)

set(expected1 "post_portfile_include_0 e76d04279b66f148165cb1c6480fedd55099cd848702e07522c9d3364841522b")
set(expected2 "post_portfile_include_1 ce31229919b5f3c6802a5670c06cf91bdf30620127cf109b41b8283e92413e21")

if(first_line STREQUAL "${expected1}")
    message(STATUS "ABI hash succesful!")
else()
    message(FATAL_ERROR "First line in abi info is not the post include file to be hashed but:\n first_line: '${first_line}'\n expected: '${expected1}' ")
endif()
if(second_line STREQUAL "${expected2}")
    message(STATUS "ABI hash succesful!")
else()
    message(FATAL_ERROR "Second line in abi info is not the second post include file to be hashed but:\n second_line: '${second_line}'\n expected: '${expected2}' ")
endif()

if(NOT Z_VCPKG_POST_PORTFILE_INCLUDES)
  message(FATAL_ERROR "Variable Z_VCPKG_POST_PORTFILE_INCLUDES not set by vcpkg-tool!")
endif()

set(path1 "${CMAKE_CURRENT_LIST_DIR}/../test1.cmake")
cmake_path(NORMAL_PATH path1)
set(path2 "${CMAKE_CURRENT_LIST_DIR}/../test2.cmake")
cmake_path(NORMAL_PATH path2)

if(NOT Z_VCPKG_POST_PORTFILE_INCLUDES STREQUAL "${path1};${path2}")
  message(FATAL_ERROR "Z_VCPKG_POST_PORTFILE_INCLUDES ist not equal to '${path1};${path2}' (Z_VCPKG_POST_PORTFILE_INCLUDES:'${Z_VCPKG_POST_PORTFILE_INCLUDES}') ")
endif()

set(VCPKG_POST_INCLUDE_CHECK_TEST1 ON)

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
