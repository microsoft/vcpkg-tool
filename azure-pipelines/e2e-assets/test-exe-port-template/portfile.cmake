set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(TOUCH "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright")

if("release-only" IN_LIST FEATURES)
    set(VCPKG_BUILD_TYPE release)
else()
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/debug/test_exe.exe" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/release/test_exe.exe" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")

if("policy-allow-exes-in-bin" IN_LIST FEATURES)
    set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
endif()
