# This makes sure the correct version of Ninja according to vcpkg_find_acquire_program is available
# before we test x-script.
vcpkg_find_acquire_program(NINJA)
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
