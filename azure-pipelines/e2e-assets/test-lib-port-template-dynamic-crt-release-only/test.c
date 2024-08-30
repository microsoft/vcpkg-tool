#include <stdio.h>

int __cdecl test_fn() {
    puts("You called the static lib function!");
    return 42;
}
