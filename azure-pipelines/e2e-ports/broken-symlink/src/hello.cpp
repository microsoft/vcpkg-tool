#include <stdio.h>
#include "hello-broken-symlink.h"

extern "C" void hello_symlink_earth() {
    puts("hello earth!");
}
