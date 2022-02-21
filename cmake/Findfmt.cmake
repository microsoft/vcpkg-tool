option(VCPKG_DEPENDENCY_EXTERNAL_FMT "Use an external version of the fmt library" OFF)

include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9 # 8.1.1
)

if(NOT fmt_FIND_REQUIRED)
    message(FATAL_ERROR "fmt must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(fmt)
endif()
