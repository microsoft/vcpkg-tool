#if defined(_WIN32)
#include <Windows.h>
int main(void)
{
    TerminateProcess(GetCurrentProcess(), 0xFFFFFFFF);
    return -1;
}
#else
int main(void) { return -1; }
#endif
