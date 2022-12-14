#include <stdio.h>

int __cdecl use_me() {
    puts("You called the static lib function!");
    return 42;
}
