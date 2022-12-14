mkdir "%~dp0debug"
mkdir "%~dp0release"
cl /EHsc /MDd "%~dp0test.c" "%~dp0test.def" /Fe"%~dp0debug\test_dll.dll" /link /DLL
cl /EHsc /MD "%~dp0test.c" "%~dp0test.def" /Fe"%~dp0release\test_dll.dll" /link /DLL
