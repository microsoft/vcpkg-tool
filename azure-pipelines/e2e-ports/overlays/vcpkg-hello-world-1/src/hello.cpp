#include <stdio.h>
#include "hello-1.h"

extern "C" void hello_earth() {
    puts("hello earth!");
}
