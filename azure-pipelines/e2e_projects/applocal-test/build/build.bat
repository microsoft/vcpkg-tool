cd %~dp0
cl /LD mylib.cpp
cl /EHsc main.cpp mylib.lib
