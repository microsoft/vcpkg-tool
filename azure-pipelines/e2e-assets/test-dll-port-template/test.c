#include <windows.h>
#include <stdio.h>

BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID Reserved)
{
    (void)Reserved;
    if (nReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls( hDllHandle );
    }

    return TRUE;
}

int __cdecl export_me() {
    puts("You called the exported function!");
    return 42;
}
