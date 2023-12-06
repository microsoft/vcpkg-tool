if (NOT VERSION STREQUAL "1.0.0")
    message(FATAL_ERROR "\${VERSION} should be '1.0.0' but is '${VERSION}'")
endif()
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
