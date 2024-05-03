file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "MIT License.")

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include/vcpkg-msvc-2013")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/test.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include/vcpkg-msvc-2013")

if(release-only IN_LIST FEATURES)
    set(VCPKG_BUILD_TYPE "release")
else()
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/lib")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/debug/test_dll.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/debug/test_dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/release/test_dll.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/release/test_dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

if(policy IN_LIST FEATURES)
    set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
endif()
