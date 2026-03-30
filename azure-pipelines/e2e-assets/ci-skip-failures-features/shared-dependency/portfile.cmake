set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
if ("depended-on-by-skip" IN_LIST FEATURES)
    message(FATAL_ERROR "The feature 'depended-on-by-skip' should not be included.")
endif()