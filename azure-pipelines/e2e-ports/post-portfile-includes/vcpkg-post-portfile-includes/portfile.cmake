file(STRINGS "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}.vcpkg_abi_info.txt" lines)
list(FILTER lines INCLUDE REGEX "post_portfile_include_(0|1)")
list(GET lines 0 first_line)
list(GET lines 1 second_line)

set(expected1 "post_portfile_include_0 ad6ac07ed1e066eaf23af161afb36b25a3ec03af49cd3e52ceb3a91d388f23f8")
set(expected2 "post_portfile_include_1 f8b37330094530b0fc7a5606fea7b491ec0e67edd1fd8f7e1a5607f7be0a3ff2")

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
