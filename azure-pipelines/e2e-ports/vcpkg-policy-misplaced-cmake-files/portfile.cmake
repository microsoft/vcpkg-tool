file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/legitimate.cmake" "# Hello!")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "N/A")

# Avoid the empty include directory check
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/vcpkg-include-folder-policies.h" "")

if (do-install-cmake IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/cmake")
    file(WRITE "${CURRENT_PACKAGES_DIR}/cmake/some_cmake.cmake" "# Hello!")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/cmake")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/cmake/some_cmake.cmake" "# Hello!")
endif()

if(do-install-lib IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/cmake")
    file(WRITE "${CURRENT_PACKAGES_DIR}/lib/cmake/some_cmake.cmake" "# Hello!")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/lib/cmake")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/some_cmake.cmake" "# Hello!")
endif()

if (policy-skip-misplaced-cmake-files-check IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
endif()

if (policy-skip-lib-cmake-merge-check IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
endif()
