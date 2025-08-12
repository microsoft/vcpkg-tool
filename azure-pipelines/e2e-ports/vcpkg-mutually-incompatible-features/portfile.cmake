if ("a" IN_LIST FEATURES AND "c" IN_LIST FEATURES)
    message(FATAL_ERROR "a and c are mutually exclusive")
endif()

if ("b" IN_LIST FEATURES AND "d" IN_LIST FEATURES)
    message(FATAL_ERROR "b and d are mutually exclusive")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
