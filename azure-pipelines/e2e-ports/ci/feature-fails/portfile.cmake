set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
if("fail" IN_LIST FEATURES)
    message(FATAL_ERROR "Failing, triggered by feature 'fail'.")
endif()
