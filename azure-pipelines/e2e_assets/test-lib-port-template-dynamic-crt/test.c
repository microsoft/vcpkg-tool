#include <stdio.h>

#ifdef NDEBUG
int __cdecl use_ndebug() {
#else
int __cdecl use_no_ndebug() {
#endif
    puts("You called the static lib function!");
    return 42;
}
