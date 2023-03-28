Updating the copy of vcpkg shipped with VS

1. Update the copy of vcpkg in default.config in the VS repo.
2. Submit a PR which will give you a prototype of VS with that vcpkg inside.
3. Install the prototype version of VS with the vcpkg inserted. Ensure the native desktop workload is selected, and that vcpkg and cmake bits are installed. Don't forget about preinstall.
4. Open a developer command prompt and run `vcpkg integrate install` (this step hopefully removed soon)
    * This also verifies that vcpkg installed into the developer command prompt correctly.
5. Create a new C++ console project.
6. Turn on diagnostic logging.
    * Tools -> Options: Projects and Solutions\Build and Run\\MSBuild project output verbosity
7. Build the console project, check that vcpkg isn't affecting that project:
    * Lib AdditionalLibraryDirectories doesn't contain a hypothetical vcpkg installed directory
    * Target VcpkgInstallManifestDependencies doesn't run
    * Target AppLocalFromInstalled doesn't run
8. In the developer command prompt cd to the directory with the vcxproj for the console app and run:
```
vcpkg new --application
vcpkg add port zlib
```
9. Rebuild the console app, and verify the manifest mode warning is printed:

```
1>Target "VcpkgCheckManifestRoot" in file "C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\vcpkg\scripts\buildsystems\msbuild\vcpkg.targets":
1>  Task "Error" skipped, due to false condition; ('$(VcpkgEnableManifest)' == 'true' and '$(_ZVcpkgManifestRoot)' == '') was evaluated as ('false' == 'true' and 'C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\' == '').
1>  Task "Message"
1>    Task Parameter:Importance=High
1>    Task Parameter:Text=The vcpkg manifest was disabled, but we found a manifest file in C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\. You may want to enable vcpkg manifests in your properties page or pass /p:VcpkgEnableManifest=true to the msbuild invocation.
1>    The vcpkg manifest was disabled, but we found a manifest file in C:\Users\bion\source\repos\ConsoleApplication3\ConsoleApplication3\. You may want to enable vcpkg manifests in your properties page or pass /p:VcpkgEnableManifest=true to the msbuild invocation.
1>  Done executing task "Message".
```
10. Right click the console application, properties, and in the property pages change vcpkg\\Use vcpkg Manifest to "Yes"
11. Rebuild the project, observe vcpkg builds zlib.
12. Change the .cpp to:
```
#include <iostream>
#include <zlib.h>

int main()
{
    std::cout << "Hello World!\n" << ZLIB_VERSION;
}
```
13. Run the program and verify that a reasonable zlib version is printed.
14. Close Visual Studio.
15. In the directory of that vcxproj, create a CMakeLists.txt with the following content:
```
cmake_minimum_required(VERSION 3.24)
project(console-app LANGUAGES CXX)
message(WARNING "CMake Version is ${CMAKE_VERSION}")
find_package(ZLIB REQUIRED)
file(GLOB PROGRAM_SOURCES *.cpp)
add_executable(program ${PROGRAM_SOURCES})
target_link_libraries(program PRIVATE ZLIB::ZLIB)
```
16. Back in the developer command prompt, run:
```
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -S . -B build_msvc
ninja -C build_msvc
build_msvc\program.exe
```
and check that a reasonable zlib version is printed.
17. Back in the developer command prompt, verify that the copy of CMake can be customized by running:
```
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -S . -B build_artifact
ninja -C build_artifact
build_artifact\program.exe
```
and check that the cmake version acquired by artifacts is printed during the cmake configure, and that a reasonable zlib version is printed.
