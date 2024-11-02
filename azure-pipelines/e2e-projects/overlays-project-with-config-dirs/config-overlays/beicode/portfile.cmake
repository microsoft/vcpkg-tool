vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO northwindtraders/beicode
  REF 19a1f95c2f56a27ced90227b5e2754a602a08e69
  SHA512 7b2bb7acb2a8ff07bff59cfa27247a7b2cced03828919cd65cc0c8cf1f724f5f1e947ed6992dcdbc913fb470694a52613d1861eaaadbf8903e94eb9cdfe4d000
  HEAD_REF main
)

vcpkg_configure_cmake(
  SOURCE_PATH "${SOURCE_PATH}"
  PREFER_NINJA
)
vcpkg_install_cmake()
vcpkg_fixup_cmake_targets()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(
  INSTALL "${SOURCE_PATH}/LICENSE"
  DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
  RENAME copyright)