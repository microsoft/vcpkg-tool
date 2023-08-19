if (NOT VERSION STREQUAL "2020-10-10")
    message(FATAL_ERROR "\${VERSION} should be '2020-10-10' but is '${VERSION}'")
endif()
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
