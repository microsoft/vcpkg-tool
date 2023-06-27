cd %~dp0
pushd installed\bin
cl /LD mylib.cpp
popd
cl /EHsc main.cpp installed\bin\mylib.lib
