file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "N/A")

# Avoid the empty include directory check
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/vcpkg-misplaced-regular-files.h" "")

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug")
file(WRITE "${CURRENT_PACKAGES_DIR}/debug/.DS_Store" "")
file(WRITE "${CURRENT_PACKAGES_DIR}/debug/bad_debug_file.txt" "")
file(WRITE "${CURRENT_PACKAGES_DIR}/.DS_Store" "")
file(WRITE "${CURRENT_PACKAGES_DIR}/bad_file.txt" "")

if (policy IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_MISPLACED_REGULAR_FILES_CHECK enabled)
endif()
