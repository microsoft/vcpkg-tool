set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

if(copyright-directory IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright")
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright/LICENSE.txt" "this is some license text")
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright/COPYING" "this is some different license text")
else()
    # Intentionally do not create "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright"
endif()

if(source IN_LIST FEATURES)
    set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/v1.16.3-6f5be3c3eb.clean")
    file(MAKE_DIRECTORY "${SOURCE_PATH}")
    file(WRITE "${SOURCE_PATH}/LICENSE.txt" "this is some license text")
endif()

if(source2 IN_LIST FEATURES)
    set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/v1.3.1-2e5db616bf.clean")
    file(MAKE_DIRECTORY "${SOURCE_PATH}")
    file(WRITE "${SOURCE_PATH}/LICENSE.txt" "this is some license text")
    file(WRITE "${SOURCE_PATH}/COPYING" "this is some different license text")
endif()

if (policy IN_LIST FEATURES)
    set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
endif()
