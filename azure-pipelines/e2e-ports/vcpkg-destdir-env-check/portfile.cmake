if(DEFINED ENV{DESTDIR})
    message(FATAL_ERROR "DESTDIR leaked into port build environment")
endif()

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
