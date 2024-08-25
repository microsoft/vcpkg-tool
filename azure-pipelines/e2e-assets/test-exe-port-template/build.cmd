mkdir "%~dp0debug"
mkdir "%~dp0release"
cl /MDd "%~dp0test.c" /Fo"%~dp0debug\test.obj" /Fe"%~dp0debug\test_exe.exe"
cl /MD "%~dp0test.c" /Fo"%~dp0release\test.obj" /Fe"%~dp0release\test_exe.exe"
