vcpkg's fuzzers use libfuzzer: https://llvm.org/docs/LibFuzzer.html

To do fuzz testing, build with VCPKG_BUILD_FUZZING=ON and VCPKG_FUZZER_INSTRUMENTATION=ON.
CMakePresets has presets for Windows and Linux to do this, Win-x64-Fuzzing and linux-fuzzing.

Then, run one of the fuzz testing binaries to form an initial corpus with:

```
cd out\build\Win-x64-Fuzzing
mkdir pick-a-corpus-dir-name
.\vcpkg-fuzz-<PARSER>.exe .\pick-a-corpus-dir-name "..\..\..\fuzzing-corpora\<PARSER>" -merge=1
```

then do a fuzzing run with:

```
.\vcpkg-fuzz-<PARSER>.exe .\pick-a-corpus-dir-name -print_final_stats=1 -jobs=N -workers=N -max_total_time=M
```

where N is the number of CPUs you want to use and M is the number of seconds you want fuzzing to run for.
