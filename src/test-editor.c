#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char* argv[])
{
    const char* path;
    FILE* f;

    path = getenv("VCPKG_TEST_OUTPUT");
    if (!path)
    {
        puts("bad env var");
        return 1;
    }

    f = fopen(path, "wb");
    if (!f)
    {
        puts("bad open");
        return 1;
    }

    for (size_t idx = 1; idx < argc; ++idx)
    {
        if (fputs(argv[idx], f) < 0)
        {
            puts("bad write");
        }

        if (fputs("\n", f) < 0)
        {
            puts("bad write newline");
        }
    }

    fclose(f);
}
