#include <stdio.h>
#include <string.h>

// This program reads stdin and asserts that it is the expected value repeated
// until stdin ends

int main(int argc, char** argv)
{
    char buffer[20];
    // The repeated string 'example' is intentionally a prime length to make hitting buffering edge
    // cases more likely
    const char expected[] = "exampleexampleexampleexamp";
    size_t offset = 0; // always between 0 and 6
    for (;;)
    {
        size_t read_amount = fread(buffer, 1, sizeof(buffer), stdin);
        if (argc > 1)
        {
            puts(argv[1]);
            fflush(stdout);
        }
        if (read_amount == 0)
        {
            if (feof(stdin))
            {
                puts("success");
                return 0;
            }
            return 1;
        }

        if (memcmp(buffer, expected + offset, read_amount) != 0)
        {
            return 2;
        }
        offset = (offset + read_amount) % 7;
    }
}
