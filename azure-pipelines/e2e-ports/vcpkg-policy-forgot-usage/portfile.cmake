set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "this is some license text")
if (policy IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_USAGE_INSTALL_CHECK enabled)
endif()
