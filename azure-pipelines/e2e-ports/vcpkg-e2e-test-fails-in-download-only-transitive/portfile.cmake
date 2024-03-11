set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "copyright message")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/installed.txt" "${PORT} installed")
