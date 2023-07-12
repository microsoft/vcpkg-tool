cmake_minimum_required(VERSION 3.24)
project(vcpkg-internal-dll-with-no-exports LANGUAGES C)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test.c" [[ #include <windows.h>
BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID Reserved)
{
    (void)Reserved;
    if (nReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls( hDllHandle );
    }

    return TRUE;
}
]])

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test.h" [[
    // empty test header
]])

add_library(no_exports "${CMAKE_CURRENT_BINARY_DIR}/test.c")
include(GNUInstallDirs)
install(TARGETS no_exports)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/test.c" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
