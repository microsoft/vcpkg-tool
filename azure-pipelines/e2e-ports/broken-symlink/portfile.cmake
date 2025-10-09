# This block is more or less identical to vcpkg-hello-world-1 because that ensures all the
# post-build checks actually run

set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src")
file(REMOVE_RECURSE "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/src" DESTINATION "${CURRENT_BUILDTREES_DIR}")
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME broken-symlink)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../../LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

file(CREATE_LINK "definitely-nonexistent-target-file.txt" "${CURRENT_PACKAGES_DIR}/share/${PORT}/broken-symlink" SYMBOLIC)
file(CREATE_LINK "self-symlink-b" "${CURRENT_PACKAGES_DIR}/share/${PORT}/self-symlink-a" SYMBOLIC)
file(CREATE_LINK "self-symlink-a" "${CURRENT_PACKAGES_DIR}/share/${PORT}/self-symlink-b" SYMBOLIC)
