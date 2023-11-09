cd %~dp0
cl /c static-lib.cpp /Fostatic-lib.obj
lib static-lib.obj /out:static-lib.lib
