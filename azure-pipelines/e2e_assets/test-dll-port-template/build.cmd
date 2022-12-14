mkdir "%~dp0debug"
mkdir "%~dp0release"
cl /MDd "%~dp0test.c" "%~dp0test.def" /Fo"%~dp0debug\test.obj" /Fe"%~dp0debug\test_dll.dll" /link /DLL
cl /MD "%~dp0test.c" "%~dp0test.def" /Fo"%~dp0release\test.obj" /Fe"%~dp0release\test_dll.dll" /link /DLL
