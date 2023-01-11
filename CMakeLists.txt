cmake_minimum_required(VERSION 3.15...3.24)

if(NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ===============
# === Options ===
# ===============

include(CMakeDependentOption)

option(BUILD_TESTING "Option for enabling testing" ON)
option(VCPKG_DEVELOPMENT_WARNINGS "Option for turning on all warnings" ON)
option(VCPKG_WARNINGS_AS_ERRORS "Set warnings to be errors" OFF)
option(VCPKG_BUILD_TLS12_DOWNLOADER "Enable building the tls12-downloader" OFF)
option(VCPKG_BUILD_FUZZING "Option for enabling vcpkg-fuzz support" OFF)
option(VCPKG_EMBED_GIT_SHA "Option for to fill in the Git SHA version; off by default to avoid privacy concerns out of official builds" OFF)
option(VCPKG_ADD_SOURCELINK "Option for enabling SourceLink in debug information on Windows/MSVC builds" "${VCPKG_EMBED_GIT_SHA}")
option(VCPKG_ARTIFACTS_DEVELOPMENT "Hard code path to artifacts TypeScript. Requires node.js and global install of @microsoft/rush." OFF)
option(VCPKG_OFFICIAL_BUILD "Option to cause immediate failure if variables required for official builds are unset." OFF)
set(VCPKG_PDB_SUFFIX "" CACHE STRING "Append this string to the name of the PDB for shipping vcpkg binaries.")

CMAKE_DEPENDENT_OPTION(VCPKG_BUILD_BENCHMARKING "Option for enabling benchmarking" OFF
    "BUILD_TESTING" OFF)

if(DEFINE_DISABLE_METRICS OR VCPKG_DISABLE_METRICS)
    message(DEPRECATION "DEFINE_DISABLE_METRICS / VCPKG_DISABLE_METRICS are now handled by creating a "
        "file vcpkg.disable_metrics next to the binary.")
endif()

set(LANGUAGES "CXX")
if(VCPKG_BUILD_TLS12_DOWNLOADER)
    list(APPEND LANGUAGES "C")
endif()

if (VCPKG_ARTIFACTS_DEVELOPMENT)
    # https://gitlab.kitware.com/cmake/cmake/-/issues/20245
    cmake_minimum_required(VERSION 3.17)
endif()

project(vcpkg
    DESCRIPTION "vcpkg helps you manage C and C++ libraries on Windows, Linux and MacOS."
    HOMEPAGE_URL "https://github.com/microsoft/vcpkg"
    LANGUAGES ${LANGUAGES}
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(cmake/utilities.cmake)

# =============
# === Files ===
# =============

file(GLOB VCPKGLIB_SOURCES CONFIGURE_DEPENDS "src/vcpkg/*.cpp")
file(GLOB VCPKGLIB_BASE_SOURCES CONFIGURE_DEPENDS "src/vcpkg/base/*.cpp")
file(GLOB VCPKGLIB_INCLUDES CONFIGURE_DEPENDS "include/vcpkg/*.h" "include/vcpkg/fwd/*.h")
file(GLOB VCPKGLIB_BASE_INCLUDES CONFIGURE_DEPENDS "include/vcpkg/base/*.h" "include/vcpkg/base/fwd/*.h")

set(VCPKG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.cpp")

file(GLOB VCPKG_TEST_SOURCES CONFIGURE_DEPENDS "src/vcpkg-test/*.cpp")
file(GLOB VCPKG_TEST_INCLUDES CONFIGURE_DEPENDS "include/vcpkg-test/*.h")

file(GLOB VCPKG_FUZZ_SOURCES CONFIGURE_DEPENDS "src/vcpkg-fuzz/*.cpp")

set(TLS12_DOWNLOAD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tls12-download.c")

file(GLOB LOCALE_RESOURCES CMAKE_CONFIGURE_DEPENDS "locales/*.json")

# ========================
# === System detection ===
# ========================

vcpkg_detect_compiler()

if(VCPKG_EMBED_GIT_SHA)
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE VCPKG_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(VCPKG_GIT_STATUS STREQUAL "")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE VCPKG_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else()
        message(WARNING "Skipping embedding SHA due to local changes.")
    endif()
endif()

if(NOT DEFINED VCPKG_VERSION OR VCPKG_VERSION STREQUAL "")
    set(VCPKG_VERSION "unknownhash")
endif()

if(NOT DEFINED VCPKG_BASE_VERSION OR VCPKG_BASE_VERSION STREQUAL "")
    if(VCPKG_OFFICIAL_BUILD)
        message(FATAL_ERROR "VCPKG_BASE_VERSION must be set for official builds.")
    endif()

    # The first digit is 2 to work with vcpkg_minimum_required in scripts.
    set(VCPKG_BASE_VERSION "2999-12-31")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    # either MSVC, or clang-cl
    string(APPEND CMAKE_C_FLAGS " -FC -permissive- -utf-8 /guard:cf")
    string(APPEND CMAKE_CXX_FLAGS " /EHsc -FC -permissive- -utf-8 /guard:cf")
    string(APPEND CMAKE_C_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")

    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO " /DEBUG /INCREMENTAL:NO /debugtype:cv,fixup /guard:cf")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /INCREMENTAL:NO /debugtype:cv,fixup /guard:cf")
    if (MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO " /CETCOMPAT")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /CETCOMPAT")
    endif()

    if(VCPKG_DEVELOPMENT_WARNINGS)
        string(APPEND CMAKE_C_FLAGS " /W4 /sdl")
        string(APPEND CMAKE_CXX_FLAGS " /W4 /sdl")
        if(VCPKG_COMPILER STREQUAL "clang")
            string(APPEND CMAKE_C_FLAGS " -Wmissing-prototypes -Wno-missing-field-initializers")
            string(APPEND CMAKE_CXX_FLAGS " -Wmissing-prototypes -Wno-missing-field-initializers")
        else()
            # -wd6553 is to workaround a violation in the Windows SDK
            # c:\program files (x86)\windows kits\10\include\10.0.22000.0\um\winreg.h(780) : warning C6553: The annotation for function 'RegOpenKeyExW' on _Param_(3) does not apply to a value type.
            string(APPEND CMAKE_C_FLAGS " -analyze -analyze:stacksize 39000 -wd6553")
            string(APPEND CMAKE_CXX_FLAGS " -analyze -analyze:stacksize 39000 -wd6553")
        endif()
    endif()

    if(VCPKG_WARNINGS_AS_ERRORS)
        string(APPEND CMAKE_C_FLAGS " /WX")
        string(APPEND CMAKE_CXX_FLAGS " /WX")
    endif()
else()
    # Neither MSVC nor clang-cl
    if(VCPKG_DEVELOPMENT_WARNINGS)
        # GCC and clang have different names for the same warning
        if(VCPKG_COMPILER STREQUAL "gcc")
            set(DECL_WARNING "-Wmissing-declarations")
        elseif(VCPKG_COMPILER STREQUAL "clang")
            set(DECL_WARNING "-Wmissing-prototypes -Wno-range-loop-analysis")
        endif()

        string(APPEND CMAKE_C_FLAGS " -Wall -Wextra -Wpedantic -Wno-unknown-pragmas -Wno-missing-field-initializers ${DECL_WARNING}")
        string(APPEND CMAKE_CXX_FLAGS " -Wall -Wextra -Wpedantic -Wno-unknown-pragmas -Wno-missing-field-initializers -Wno-redundant-move ${DECL_WARNING}")
    endif()

    if(VCPKG_WARNINGS_AS_ERRORS)
        string(APPEND CMAKE_C_FLAGS " -Werror")
        string(APPEND CMAKE_CXX_FLAGS " -Werror")
    endif()
endif()

if(APPLE)
    set(CMAKE_C_ARCHIVE_CREATE   "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
    set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
endif()

# =======================
# === Paths detection ===
# =======================

include(GNUInstallDirs)

# ====================
# === Dependencies ===
# ====================

find_package(fmt REQUIRED)
find_package(CMakeRC REQUIRED)

# === Target: locale-resources ===
cmrc_add_resource_library(locale-resources ALIAS cmakerc::locales NAMESPACE cmakerc ${LOCALE_RESOURCES})
if(NOT MSVC)
    if(VCPKG_COMPILER STREQUAL "gcc")
        target_compile_options(locale-resources PRIVATE
            -Wno-missing-declarations
            )
    elseif(VCPKG_COMPILER STREQUAL "clang")
        target_compile_options(locale-resources PRIVATE
            -Wno-missing-prototypes
            )
    endif()
endif()

# === Target: vcpkglib ===

add_library(vcpkglib OBJECT
    ${VCPKGLIB_BASE_SOURCES}
    ${VCPKGLIB_SOURCES}
    ${VCPKGLIB_BASE_INCLUDES}
    ${VCPKGLIB_INCLUDES}
)
target_include_directories(vcpkglib PUBLIC include)

target_compile_definitions(vcpkglib PUBLIC
    VCPKG_VERSION=${VCPKG_VERSION}
    VCPKG_BASE_VERSION=${VCPKG_BASE_VERSION}
)

if(NOT DEFINED VCPKG_STANDALONE_BUNDLE_SHA OR VCPKG_STANDALONE_BUNDLE_SHA STREQUAL "")
    if(VCPKG_OFFICIAL_BUILD)
        message(FATAL_ERROR "VCPKG_STANDALONE_BUNDLE_SHA is required for official builds.")
    endif()
else()
    target_compile_definitions(vcpkglib PUBLIC
        VCPKG_STANDALONE_BUNDLE_SHA=${VCPKG_STANDALONE_BUNDLE_SHA}
    )
endif()

if(NOT DEFINED VCPKG_CE_SHA OR VCPKG_CE_SHA STREQUAL "")
    if(VCPKG_OFFICIAL_BUILD)
        message(FATAL_ERROR "VCPKG_CE_SHA is required for official builds.")
    endif()
    if (VCPKG_ARTIFACTS_DEVELOPMENT)
        file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ce/ce" VCPKG_ARTIFACTS_PATH)
        target_compile_definitions(vcpkglib PUBLIC "VCPKG_ARTIFACTS_PATH=${VCPKG_ARTIFACTS_PATH}")
        mark_as_advanced(VCPKG_ARTIFACTS_PATH)

        if (WIN32)
            set(RUSH_SUFFIX ".cmd")
        else()
            set(RUSH_SUFFIX "")
        endif()

        find_program(NODEJS "node")
        if (NOT NODEJS)
            message(FATAL_ERROR "node.js and @microsoft/rush must be installed when VCPKG_ARTIFACTS_DEVELOPMENT is set")
        endif()

        find_program(RUSH "rush${RUSH_SUFFIX}")
        if (NOT RUSH)
            message(FATAL_ERROR "@microsoft/rush is required when VCPKG_ARTIFACTS_DEVELOPMENT is set; use `npm install -g @microsoft/rush`")
        endif()

        add_custom_command(
            OUTPUT
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/ce/node_modules"
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/test/node_modules"
            COMMAND "${RUSH}" ARGS "update"
            COMMAND "${RUSH}" ARGS "rebuild"
            WORKING_DIRECTORY
                "${CMAKE_CURRENT_SOURCE_DIR}/ce"
            COMMENT
                "Running rush update..."
            VERBATIM
            )

        add_custom_target(rush-update
            ALL
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/ce/node_modules"
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/test/node_modules"
                )

# === Target: vcpkg-artifacts ===
        file(GLOB_RECURSE VCPKG_ARTIFACTS_SOURCES CONFIGURE_DEPENDS "ce/ce/*.ts")
        add_custom_command(
            OUTPUT
                "${CMAKE_CURRENT_LIST_DIR}/ce/ce/dist/tsconfig.tsbuildinfo"
            COMMAND
                "${NODEJS}" "${CMAKE_CURRENT_LIST_DIR}/ce/ce/node_modules/typescript/bin/tsc"
                    -p "${CMAKE_CURRENT_LIST_DIR}/ce/ce"
            DEPENDS
                ${VCPKG_ARTIFACTS_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/ce/node_modules"
            COMMENT
                "Building vcpkg-artifacts..."
            VERBATIM
            )

        add_custom_target(vcpkg-artifacts
            ALL
            DEPENDS
                "${CMAKE_CURRENT_LIST_DIR}/ce/ce/dist/tsconfig.tsbuildinfo"
            )

# === Target: vcpkg-artifacts-test ===
        file(GLOB_RECURSE VCPKG_ARTIFACTS_TEST_SOURCES CONFIGURE_DEPENDS "ce/test/*.ts")
        add_custom_command(
            OUTPUT
                "${CMAKE_CURRENT_LIST_DIR}/ce/test/dist/tsconfig.tsbuildinfo"
            COMMAND
                "${NODEJS}" "${CMAKE_CURRENT_LIST_DIR}/ce/test/node_modules/typescript/bin/tsc"
                    -p "${CMAKE_CURRENT_LIST_DIR}/ce/test"
            DEPENDS
                ${VCPKG_ARTIFACTS_TEST_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/ce/test/node_modules"
            COMMENT
                "Building vcpkg-artifacts-test..."
            VERBATIM
            )

        add_custom_target(vcpkg-artifacts-test
            ALL
            DEPENDS
                "${CMAKE_CURRENT_LIST_DIR}/ce/test/dist/tsconfig.tsbuildinfo"
            )
    endif()
else()
    if (VCPKG_ARTIFACTS_DEVELOPMENT)
        message(WARNING "VCPKG_CE_SHA overrides VCPKG_ARTIFACTS_DEVELOPMENT")
    endif()
    target_compile_definitions(vcpkglib PUBLIC
        VCPKG_CE_SHA=${VCPKG_CE_SHA}
    )
endif()

set(CPP_ATOMIC_LIBRARY "")
include(CheckCXXSourceCompiles)
if(NOT MSVC)
    # Some platforms require a explicit linkage against libatomic to operate on 64-bit numbers
    set(TEST_SOURCE "
#include <stdint.h>
#include <atomic>
std::atomic<uintptr_t> x;
std::atomic<uintmax_t> y;
int main() {
    return x + y;
}
")
    check_cxx_source_compiles("${TEST_SOURCE}" CPP_ATOMIC_BUILTIN)
    if(NOT CPP_ATOMIC_BUILTIN)
        list(APPEND CMAKE_REQUIRED_LIBRARIES atomic)
        set(CPP_ATOMIC_LIBRARY atomic)
        check_cxx_source_compiles("${TEST_SOURCE}" CPP_ATOMIC_WITH_LIBATOMIC)
        if (NOT CPP_ATOMIC_WITH_LIBATOMIC)
            message(
                FATAL_ERROR "unable to link C++ std::atomic code: you may need \
                to install GNU libatomic"
            )
        endif()
    endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vcpkglib
    PUBLIC
        fmt::fmt
        cmakerc::locales
    PRIVATE
        Threads::Threads
        ${CPP_ATOMIC_LIBRARY}
)

if(MSVC)
    get_target_property(_srcs vcpkglib SOURCES)

    if(NOT CMAKE_GENERATOR MATCHES "Visual Studio .*")
        set_property(SOURCE src/pch.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
        set_property(SOURCE ${_srcs} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
    endif()

    set_source_files_properties(src/pch.cpp PROPERTIES COMPILE_FLAGS "/Ycpch.h")
    target_sources(vcpkglib PRIVATE src/pch.cpp)
    target_compile_options(vcpkglib PRIVATE /Yupch.h /FIpch.h /Zm200)
else()
    target_compile_options(vcpkglib PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h")
endif()

if(MINGW)
    target_compile_definitions(vcpkglib
        PUBLIC
            UNICODE
            _WIN32_WINNT=0x0601
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY=4
            __fastfail=exit
    )
    target_link_libraries(vcpkglib PUBLIC winhttp bcrypt version ole32 uuid)
endif()

# === Target: vcpkg ===
add_custom_command(
    OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/vcpkg.ps1"
    COMMAND
        "${CMAKE_COMMAND}" ARGS -E copy "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg-in-development.ps1" "${CMAKE_CURRENT_BINARY_DIR}/vcpkg.ps1"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg-in-development.ps1"
    VERBATIM
    )

add_custom_target(vcpkg-ps1 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/vcpkg.ps1")

add_executable(vcpkg ${VCPKG_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
target_link_libraries(vcpkg PRIVATE vcpkglib)

if(VCPKG_ADD_SOURCELINK)
    if(VCPKG_VERSION STREQUAL "unknownhash")
        message(FATAL_ERROR "Attempted to add source link information, but there was no git SHA defined. VCPKG_ADD_SOURCELINK only works if VCPKG_EMBED_GIT_SHA is set.")
    endif()
    vcpkg_target_add_sourcelink(vcpkg
        REPO "microsoft/vcpkg-tool"
        REF "${VCPKG_VERSION}"
    )
endif()
if(VCPKG_PDB_SUFFIX)
    set_property(TARGET vcpkg PROPERTY PDB_NAME "vcpkg${VCPKG_PDB_SUFFIX}")
endif()

# === Target: generate-message-map ===

add_custom_target(generate-message-map
   COMMAND vcpkg x-generate-default-message-map locales/messages.json
   COMMAND vcpkg x-generate-default-message-map --no-output-comments locales/messages.en.json
   WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
   COMMENT "Update locales/messages.json"
   VERBATIM
)

# === Target: vcpkg-test ===

if (BUILD_TESTING)
    enable_testing()

    add_executable(vcpkg-test
        ${VCPKG_TEST_SOURCES}
        ${VCPKG_TEST_INCLUDES}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest"
    )
    target_link_libraries(vcpkg-test PRIVATE vcpkglib)
    if(ANDROID)
        target_link_libraries(vcpkg-test PRIVATE log)
    endif()

    add_test(NAME default COMMAND vcpkg-test --order rand --rng-seed time)

    if (VCPKG_BUILD_BENCHMARKING)
        target_compile_options(vcpkg-test PRIVATE -DCATCH_CONFIG_ENABLE_BENCHMARKING)
    endif()
endif()

# === Target: vcpkg-fuzz ===

if(VCPKG_BUILD_FUZZING)
    add_executable(vcpkg-fuzz ${VCPKG_FUZZ_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
    target_link_libraries(vcpkg-fuzz PRIVATE vcpkglib)
endif()


# === Target: tls12-download ===

if(VCPKG_BUILD_TLS12_DOWNLOADER)
    add_executable(tls12-download ${TLS12_DOWNLOAD_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
    set_property(TARGET tls12-download PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET tls12-download APPEND PROPERTY LINK_OPTIONS "$<IF:$<CONFIG:Debug>,,/ENTRY:entry>")
    target_link_libraries(tls12-download winhttp wintrust shell32)
    if(VCPKG_PDB_SUFFIX)
        set_property(TARGET tls12-download PROPERTY PDB_NAME "tls12-download${VCPKG_PDB_SUFFIX}")
    endif()
endif()


# === Target: format ===

find_program(CLANG_FORMAT clang-format PATHS "$ENV{PROGRAMFILES}/LLVM/bin")
if(CLANG_FORMAT)
    # doing all of these formats in one line has a tendency to overflow the command line length
    add_custom_target(format
        COMMAND "${CLANG_FORMAT}" -i -verbose ${CMAKE_CURRENT_SOURCE_DIR}/src/pch.cpp
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_BASE_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_BASE_INCLUDES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_INCLUDES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_SOURCES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_TEST_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_TEST_INCLUDES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_FUZZ_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${TLS12_DOWNLOAD_SOURCES}
    )
endif()

# ====================
# === Installation ===
# ====================

install(TARGETS vcpkg
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
)
