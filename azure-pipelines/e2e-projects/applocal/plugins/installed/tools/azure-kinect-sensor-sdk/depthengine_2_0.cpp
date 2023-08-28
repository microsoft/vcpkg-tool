#define MYLIB_EXPORTS 1

#include <stdio.h>
class __declspec(dllexport) depthengine_2_0 {
public:
   static void my_func();
};

void depthengine_2_0::my_func() {
    puts("hello world");
}
