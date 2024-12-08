set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
if(APPLE)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
elseif(UNIX)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
endif()
set(VCPKG_HASH_ADDITIONAL_FILES "additional_abi_file.txt") # relatives path should fail. 
