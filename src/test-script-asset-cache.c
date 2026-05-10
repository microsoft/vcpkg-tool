#define _CRT_SECURE_NO_WARNINGS

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char expected_uri[] =
    "https://raw.githubusercontent.com/microsoft/vcpkg-tool/1767aaee7b229c609f7ad5cf2f57b6a6cc309fb8/LICENSE.txt";
// Note that this checks that the SHA is properly tolower'd
static const char expected_sha[] = "65077997890f66f6041bb3284bb7b88e27631411ccbc253201ca4e00c4bcc58c0d77edffda497549879"
                                   "7cc10772c7fd68fbeb13cc4ac493a3471a9d49e5b6f24";

static const char result_data[] = //
    "Copyright (c) Microsoft Corporation\n"
    "\n"
    "All rights reserved.\n"
    "\n"
    "MIT License\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy of\n"
    "this software and associated documentation files (the \"Software\"), to deal in\n"
    "the Software without restriction, including without limitation the rights to\n"
    "use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies\n"
    "of the Software, and to permit persons to whom the Software is furnished to do\n"
    "so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all\n"
    "copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
    "SOFTWARE.\n";

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
