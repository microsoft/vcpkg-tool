#if defined(_WIN32)
#include <Windows.h>
int main() { CloseHandle(GetStdHandle(STD_INPUT_HANDLE)); }
#else
#include <unistd.h>
int main(void) { close(STDIN_FILENO); }
#endif
