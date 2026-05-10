set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
if ("fail-if-included" IN_LIST FEATURES)
    message(FATAL_ERROR "The feature 'fail-if-included' should not be included.")
endif()
