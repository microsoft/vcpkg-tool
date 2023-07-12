#include <stdio.h>
#if defined(_WIN32)
#include <Windows.h>
#else // ^^^ _WIN32 // !_WIN32
#include <unistd.h>
#endif // ^^^ !_WIN32

int main()
{
    const char content[] = "hello world";
    fwrite(content, 1, sizeof(content) - 1, stdout);
    fflush(stdout);
#if defined(_WIN32)
    CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
#else  // ^^^ _WIN32 // !_WIN32
    close(STDOUT_FILENO);
#endif // ^^^ !_WIN32
}
