file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "N/A")

# Avoid the empty include directory check
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
file(WRITE "${CURRENT_PACKAGES_DIR}/include/vcpkg-misplaced-pkgconfig.h" "")

set(ARCH_DEPENDENT_DEBUG_PC_CONTENT [[prefix=${pcfiledir}/../..
exec_prefix=${prefix}
libdir=${prefix}/lib
sharedlibdir=${prefix}/lib
includedir=${prefix}/../include

Name: zlib
Description: zlib compression library
Version: 1.3.1


Libs: "-L${libdir}" "-L${sharedlibdir}" -lz
Requires: 
Cflags: "-I${includedir}"
]])

if(install-arch-dependent-good IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc" "${ARCH_DEPENDENT_DEBUG_PC_CONTENT}")
endif()

set(ARCH_DEPENDENT_PC_CONTENT [[prefix=${pcfiledir}/../..
exec_prefix=${prefix}
libdir=${prefix}/lib
sharedlibdir=${prefix}/lib
includedir=${prefix}/include

Name: zlib
Description: zlib compression library
Version: 1.3.1

Requires:
Libs: "-L${libdir}" "-L${sharedlibdir}" -lzlib
Cflags: "-I${includedir}"
]])

if(install-arch-dependent-good-release-only IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc" "${ARCH_DEPENDENT_PC_CONTENT}")
endif()

if(install-arch-dependent-bad-share IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib.pc" "${ARCH_DEPENDENT_PC_CONTENT}")
endif()

if(install-arch-dependent-bad-misplaced IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/bin/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/bin/pkgconfig/zlib.pc" "${ARCH_DEPENDENT_DEBUG_PC_CONTENT}")
endif()

if(install-arch-dependent-bad-misplaced-release-only IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib.pc" "${ARCH_DEPENDENT_PC_CONTENT}")
endif()

set(ARCH_AGNOSTIC_WITH_EMPTY_LIBS_PC_CONTENT [[prefix=${pcfiledir}/../..
exec_prefix=${prefix}
libdir=${prefix}/lib
sharedlibdir=${prefix}/lib
includedir=${prefix}/include

Name: zlib
Description: zlib compression library
Version: 1.3.1

Requires:
Libs:
Cflags: "-I${includedir}"
]])

if(install-arch-agnostic-empty-libs-good IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib-no-libs.pc" "${ARCH_AGNOSTIC_WITH_EMPTY_LIBS_PC_CONTENT}")
endif()

if(install-arch-agnostic-empty-libs-good-share IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib-no-libs.pc" "${ARCH_AGNOSTIC_WITH_EMPTY_LIBS_PC_CONTENT}")
endif()

if(install-arch-agnostic-empty-libs-bad-misplaced IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib-no-libs.pc" "${ARCH_AGNOSTIC_WITH_EMPTY_LIBS_PC_CONTENT}")
endif()

set(ARCH_AGNOSTIC_PC_CONTENT [[prefix=${pcfiledir}/../..
exec_prefix=${prefix}
includedir=${prefix}/include

Name: libmorton
Description: C++ header-only library to efficiently encode/decode Morton codes in/from 2D/3D coordinates
URL: https://github.com/Forceflow/libmorton
Version: 0.2.8


Cflags: "-I${includedir}"
]])

if(install-arch-agnostic-good IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libmorton.pc" "${ARCH_AGNOSTIC_PC_CONTENT}")
endif()

if(install-arch-agnostic-good-share IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/pkgconfig/libmorton.pc" "${ARCH_AGNOSTIC_PC_CONTENT}")
endif()

if(install-arch-agnostic-bad-misplaced IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin/pkgconfig")
    file(WRITE "${CURRENT_PACKAGES_DIR}/bin/pkgconfig/libmorton.pc" "${ARCH_AGNOSTIC_PC_CONTENT}")
endif()

if (policy IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
endif()
