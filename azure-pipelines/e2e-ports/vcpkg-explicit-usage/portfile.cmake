set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" [[This is some usage text explicitly set by the port.

This output should end up on the console.

]])
