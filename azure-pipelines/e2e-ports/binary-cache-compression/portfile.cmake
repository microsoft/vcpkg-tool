set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
string(REPEAT "binary cache compression test\n" 10000 contents)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "${contents}")
