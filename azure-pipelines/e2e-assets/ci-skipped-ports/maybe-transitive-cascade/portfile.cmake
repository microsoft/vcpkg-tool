set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
if(NOT EXISTS "${CURRENT_INSTALLED_DIR}/share/maybe-skip")
    message(FATAL_ERROR "Installation order violation")
endif()
