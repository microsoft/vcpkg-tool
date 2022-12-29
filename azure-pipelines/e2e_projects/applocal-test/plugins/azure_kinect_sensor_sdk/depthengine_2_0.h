#include <stdio.h>

class 
#if MYLIB_EXPORTS
__declspec( dllexport ) 
#endif
depthengine_2_0 {
public:
   static void my_func();
};