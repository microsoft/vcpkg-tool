vcpkg_minimum_required(VERSION 2024-11-01)

message(STATUS "Installing skip-dep")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "Test port")

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
