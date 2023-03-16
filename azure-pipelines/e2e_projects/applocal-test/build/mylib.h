#include <stdio.h>

class 
#if MYLIB_EXPORTS
__declspec( dllexport ) 
#endif
mylib {
public:
   static void my_func();
};