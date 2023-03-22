file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
vcpkg_touch(TARGET "${CURRENT_PACKAGES_DIR}/include/vcpkg-uses-touch.h")
file(INSTALL "${VCPKG_ROOT_DIR}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
