#define _CRT_SECURE_NO_WARNINGS

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char expected_uri[] = "https://example.com/hello-world.txt";
// Note that this checks that the SHA is properly tolower'd
static const char expected_sha[] = "09e1e2a84c92b56c8280f4a1203c7cffd61b162cfe987278d4d6be9afbf38c0e8934cdadf83751f4e99"
                                   "d111352bffefc958e5a4852c8a7a29c95742ce59288a8";

static const char result_data[] = "Hello, world!\n";

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        puts("Bad argument count; usage: test-script-asset-cache {url} {sha512} {dst}");
        return 1;
    }

    if (strcmp(argv[1], expected_uri) != 0)
    {
        printf("Bad argument 1; expected url: %s, got %s\n", expected_uri, argv[1]);
        return 1;
    }

    if (strcmp(argv[2], expected_sha) != 0)
    {
        printf("Bad argument 2; expected sha512: %s, got %s\n", expected_sha, argv[2]);
        return 1;
    }

    char* destination = argv[3];
#if defined(_WIN32)
    if (!((destination[0] >= 'A' && destination[0] <= 'Z') || (destination[0] >= 'a' && destination[0] <= 'z')) ||
        destination[1] != ':' || (destination[2] != '/' && destination[2] != '\\'))
    {
        printf("Bad argument 3; expected path be absolute, got %s\n", destination);
        return 1;
    }
#else
    if (destination[0] != '/')
    {
        printf("Bad argument 3; expected path be absolute, got %s\n", destination);
        return 1;
    }
#endif

    FILE* fp = fopen(destination, "wb");
    if (!fp)
    {
        puts("fopen failed");
        return 1;
    }

    if (fwrite(result_data, 1, sizeof(result_data) - 1, fp) != sizeof(result_data) - 1)
    {
        puts("fputs failed");
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}
