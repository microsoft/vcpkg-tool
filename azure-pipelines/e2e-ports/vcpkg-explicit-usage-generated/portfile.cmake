set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src")
file(REMOVE_RECURSE "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/src" DESTINATION "${CURRENT_BUILDTREES_DIR}")
vcpkg_cmake_configure(
	SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME explicit-usage-generated)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" [[This is some usage text explicitly set by the port.

This output should end up on the console.
]])

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/../../../LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
