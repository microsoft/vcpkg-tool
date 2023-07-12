#include <stdio.h>
#include <string.h>

// This program reads stdin and asserts that it is the expected value repeated
// until stdin ends

// Intentionally a prime length to make hitting buffering edge cases more likely
const char expected_repeat[] = {'e', 'x', 'a', 'm', 'p', 'l', 'e'};

int main(int argc, char** argv)
{
    size_t consumed_prefix = 0;
    char buffer[20];
    for (;;)
    {
        size_t read_amount = fread(buffer, 1, sizeof(buffer), stdin);
        if (argc > 1)
        {
            puts(argv[1]);
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

        size_t prefix_check = read_amount;
        if (prefix_check <= (sizeof(expected_repeat) - consumed_prefix))
        {
            // check a partial suffix (this read didn't include a complete suffix)
            if (memcmp(buffer, expected_repeat + consumed_prefix, prefix_check) != 0)
            {
                return 2;
            }

            consumed_prefix += prefix_check;
            continue;
        }

        // check a whole suffix to realign to expected_repeat blocks
        prefix_check = sizeof(expected_repeat) - consumed_prefix;
        if (memcmp(buffer, expected_repeat + consumed_prefix, prefix_check) != 0)
        {
            return 3;
        }

        size_t checked = prefix_check;
        for (; read_amount - checked >= sizeof(expected_repeat); checked += sizeof(expected_repeat))
        {
            if (memcmp(buffer + checked, expected_repeat, sizeof(expected_repeat)) != 0)
            {
                return 4;
            }
        }

        consumed_prefix = read_amount - checked;
        if (memcmp(buffer + checked, expected_repeat, consumed_prefix) != 0)
        {
            return 5;
        }
    }
}
