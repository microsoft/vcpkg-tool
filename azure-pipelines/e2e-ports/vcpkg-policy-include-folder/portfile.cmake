file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "N/A")

if(do-install IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
    file(WRITE "${CURRENT_PACKAGES_DIR}/include/vcpkg-policy-include-folder.h" "")
endif()

if(do-install-debug IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/include")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/include/vcpkg-policy-include-folder.h" "")
endif()

if(do-install-debug-share IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug/share")
    file(WRITE "${CURRENT_PACKAGES_DIR}/debug/share/example.txt" "")
endif()

if(do-install-restricted IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/include")
    file(WRITE "${CURRENT_PACKAGES_DIR}/include/json.h" "")
endif()

if(do-install-vcpkg-port-config IN_LIST FEATURES)
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/vcpkg-port-config.cmake" "")
endif()

if(policy-empty-include-folder IN_LIST FEATURES)
    set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
endif()

if(policy-cmake-helper-port IN_LIST FEATURES)
    set(VCPKG_POLICY_CMAKE_HELPER_PORT enabled)
endif()

if(policy-allow-restricted-headers IN_LIST FEATURES)
    set(VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS enabled)
endif()

if(policy-allow-debug-include IN_LIST FEATURES)
    set(VCPKG_POLICY_ALLOW_DEBUG_INCLUDE enabled)
endif()

if(policy-allow-debug-share IN_LIST FEATURES)
    set(VCPKG_POLICY_ALLOW_DEBUG_SHARE enabled)
endif()
