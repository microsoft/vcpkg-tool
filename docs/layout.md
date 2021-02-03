# Layout of the vcpkg source tree

If you'd like to contribute to the vcpkg tool itself, most of your code edits will be in this repo.

## Build Files

These are the files used to build and configure the project.

### Top Level

We have six files in this directory -- one `.clang-format` file, one
`CMakeLists.txt` file, three Visual Studio files, and `VERSION.txt`.

  - `.clang-format`: This is where we store the formatting settings of the
    project. If you want to format the project, you can use the `format` target
    with the CMake build system.
  - `CMakeLists.txt`: This is where the CMake build system definition lives. If
    you want to modify how one builds the project, or add a target, you can do
    it here.
  - The Visual Studio file:
    - `vcpkg.natvis`: NATVIS files allow one to visualize objects of user
      defined type in the debugger -- this one contains the definitions for
      `vcpkg`'s types.

## Source Files

If you're modifying the project, it's likely that these are the directories that
you're going to deal with.

### `include`

There's one file in here -- `pch.h`. This contains most of the C++ standard
library, and acts as a [precompiled header]. You can read more at the link.

There are three directories:

  - `catch2` -- This contains the single-header library [catch2]. We use this
    library for both [testing] and [benchmarking].
  - `vcpkg` -- This contains the header files for the `vcpkg` project. All of
    the interfaces for building, installing, and generally "port stuff" live
    here.
    - `vcpkg/base` -- This contains the interfaces for the
      "vcpkg standard library" -- file handling, hashing, strings,
      `Span<T>`, printing, etc.
  - `vcpkg-test` -- This contains the interfaces for any common utilities
    required by the tests.

### `src`

The source files live here. `pch.cpp` is the source file for the
[precompiled header]; `vcpkg.cpp` is where the `vcpkg` binary lives.

The interesting files live in the `vcpkg` and `vcpkg-test` directories. In
`vcpkg`, you have the implementation for the interfaces that live in
`include/vcpkg`; and in `vcpkg-test`, you have the tests and benchmarks.

[precompiled header]: https://en.wikipedia.org/wiki/Precompiled_header
[catch2]: https://github.com/catchorg/Catch2
[testing]: ./testing.md
[benchmarking]: ./benchmarking.md
