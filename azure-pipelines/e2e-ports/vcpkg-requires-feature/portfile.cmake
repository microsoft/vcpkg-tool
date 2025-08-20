if(NOT "b-required" IN_LIST FEATURES)
    message(FATAL_ERROR "The required feature is not present")
endif()

if("fails" IN_LIST FEATURES)
    message(FATAL_ERROR "The fails feature is present")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
