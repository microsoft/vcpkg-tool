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
option(VCPKG_MSVC_ANALYZE "Option for turning on /analyze" ON)
option(VCPKG_WARNINGS_AS_ERRORS "Set warnings to be errors" OFF)
option(VCPKG_BUILD_TLS12_DOWNLOADER "Enable building the tls12-downloader" OFF)
option(VCPKG_BUILD_FUZZING "Option for enabling vcpkg-fuzz support" OFF)
option(VCPKG_EMBED_GIT_SHA "Option for to fill in the Git SHA version; off by default to avoid privacy concerns out of official builds" OFF)
option(VCPKG_ADD_SOURCELINK "Option for enabling SourceLink in debug information on Windows/MSVC builds" "${VCPKG_EMBED_GIT_SHA}")
option(VCPKG_ARTIFACTS_DEVELOPMENT "Hard code path to artifacts TypeScript. Requires node.js and npm." OFF)
option(VCPKG_OFFICIAL_BUILD "Option to cause immediate failure if variables required for official builds are unset." OFF)
set(VCPKG_PDB_SUFFIX "" CACHE STRING "Append this string to the name of the PDB for shipping vcpkg binaries.")

CMAKE_DEPENDENT_OPTION(VCPKG_BUILD_BENCHMARKING "Option for enabling benchmarking" OFF
    "BUILD_TESTING" OFF)

if(DEFINE_DISABLE_METRICS OR VCPKG_DISABLE_METRICS)
    message(DEPRECATION "DEFINE_DISABLE_METRICS / VCPKG_DISABLE_METRICS are now handled by creating a "
        "file vcpkg.disable_metrics next to the binary.")
endif()

project(vcpkg
    DESCRIPTION "vcpkg helps you manage C and C++ libraries on Windows, Linux and MacOS."
    HOMEPAGE_URL "https://github.com/microsoft/vcpkg"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(cmake/utilities.cmake)

# ========================
# === System detection ===
# ========================

vcpkg_detect_compiler()

if(MSVC AND NOT COMMAND target_precompile_headers)
    message(FATAL_ERROR "CMake 3.16 (target_precompile_headers) is required to build with MSVC")
endif()

if(VCPKG_EMBED_GIT_SHA)
    if(DEFINED VCPKG_VERSION)
        message(STATUS "Using supplied version SHA ${VCPKG_VERSION}.")
    else()
        find_package(Git REQUIRED)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            OUTPUT_VARIABLE VCPKG_GIT_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(VCPKG_GIT_STATUS STREQUAL "")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                OUTPUT_VARIABLE VCPKG_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            message(WARNING "Skipping embedding SHA due to local changes.")
        endif()
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

    # Avoid CMake's default taking of the pretty names
    string(REPLACE "/DAMD64" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/DAMD64" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/DARM64EC" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/DARM64EC" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    if(VCPKG_DEVELOPMENT_WARNINGS)
        string(APPEND CMAKE_C_FLAGS " /W4 /sdl")
        string(APPEND CMAKE_CXX_FLAGS " /W4 /sdl")
        if(VCPKG_COMPILER STREQUAL "clang")
            string(APPEND CMAKE_C_FLAGS " -Wmissing-prototypes -Wno-missing-field-initializers")
            string(APPEND CMAKE_CXX_FLAGS " -Wmissing-prototypes -Wno-missing-field-initializers")
        elseif(VCPKG_MSVC_ANALYZE)
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

        if(VCPKG_COMPILER STREQUAL "gcc" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
            # -Wno-dangling-reference results from https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107532 :(
            string(APPEND CMAKE_CXX_FLAGS " -Wno-dangling-reference")
        endif()
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

file(GLOB VCPKGLIB_BASE_SOURCES CONFIGURE_DEPENDS "src/vcpkg/base/*.cpp")
file(GLOB VCPKGLIB_SOURCES CONFIGURE_DEPENDS "src/vcpkg/*.cpp")
file(GLOB VCPKGLIB_BASE_INCLUDES CONFIGURE_DEPENDS "include/vcpkg/base/*.h" "include/vcpkg/base/fwd/*.h")
file(GLOB VCPKGLIB_INCLUDES CONFIGURE_DEPENDS "include/vcpkg/*.h" "include/vcpkg/fwd/*.h")

set(VCPKG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.cpp")

file(GLOB VCPKG_TEST_SOURCES CONFIGURE_DEPENDS "src/vcpkg-test/*.cpp")
file(GLOB VCPKG_TEST_INCLUDES CONFIGURE_DEPENDS "include/vcpkg-test/*.h")

set(VCPKG_FUZZ_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg-fuzz/main.cpp")
set(TLS12_DOWNLOAD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/tls12-download.c")
set(CLOSES_EXIT_MINUS_ONE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/closes-exit-minus-one.c")
set(CLOSES_STDIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/closes-stdin.c")
set(CLOSES_STDOUT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/closes-stdout.c")
set(READS_STDIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/reads-stdin.c")
set(TEST_EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test-editor.c")
set(TEST_SCRIPT_ASSET_CACHE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test-script-asset-cache.c")

# ====================
# === Dependencies ===
# ====================

find_package(fmt REQUIRED)
find_package(CMakeRC REQUIRED)

# === Target: locale-resources ===

file(GLOB LOCALE_RESOURCES CMAKE_CONFIGURE_DEPENDS "locales/*.json")
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
    _FILE_OFFSET_BITS=64
)

if(VCPKG_STANDALONE_BUNDLE_SHA)
    target_compile_definitions(vcpkglib PUBLIC
        "VCPKG_STANDALONE_BUNDLE_SHA=${VCPKG_STANDALONE_BUNDLE_SHA}"
    )
elseif(VCPKG_OFFICIAL_BUILD)
    message(FATAL_ERROR "VCPKG_STANDALONE_BUNDLE_SHA is required for official builds.")
endif()

if(VCPKG_ARTIFACTS_SHA)
    target_compile_definitions(vcpkglib PUBLIC
        "VCPKG_ARTIFACTS_SHA=${VCPKG_ARTIFACTS_SHA}"
    )
elseif(VCPKG_OFFICIAL_BUILD)
    message(FATAL_ERROR "VCPKG_ARTIFACTS_SHA is required for official builds.")
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-artifacts" VCPKG_ARTIFACTS_SOURCE_DIR)
if (VCPKG_ARTIFACTS_DEVELOPMENT)
    # The directory constructed by this portion of the build script needs to be kept in sync with
    # that created by azure-pipelines/signing.yml

    if (WIN32)
        set(NPM_SUFFIX ".cmd")
    else()
        set(NPM_SUFFIX "")
    endif()

    find_program(NODEJS "node")
    find_program(NPM "npm${NPM_SUFFIX}")
    if (NOT NODEJS OR NOT NPM)
        message(FATAL_ERROR "node.js and npm must be installed when VCPKG_ARTIFACTS_DEVELOPMENT is set")
    endif()

    add_custom_command(
        OUTPUT
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules"
        COMMAND "${NPM}" ARGS "ci" "--include=dev"
        WORKING_DIRECTORY
            "${VCPKG_ARTIFACTS_SOURCE_DIR}"
        COMMENT
            "Running npm install..."
        MAIN_DEPENDENCY
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/package-lock.json"
        VERBATIM
        )

    add_custom_target(npm-restore
        ALL
        DEPENDS
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules"
            )
    set_target_properties(npm-restore
        PROPERTIES
            ADDITIONAL_CLEAN_FILES "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules"
    )

# === Target: vcpkg-artifacts-target ===
# The suffix "-target" is added to avoid a conflict in CMake with the directory named vcpkg-artifacts and the target named vcpkg-artifacts.
    file(GLOB VCPKG_ARTIFACTS_ROOT_SOURCES LIST_DIRECTORIES false RELATIVE "${VCPKG_ARTIFACTS_SOURCE_DIR}" CONFIGURE_DEPENDS "${VCPKG_ARTIFACTS_SOURCE_DIR}/*.ts")
    file(GLOB_RECURSE VCPKG_ARTIFACTS_NESTED_SOURCES LIST_DIRECTORIES false RELATIVE "${VCPKG_ARTIFACTS_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/amf/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/archivers/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/artifacts/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/cli/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/fs/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/installers/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/interfaces/*.ts"
        # "${VCPKG_ARTIFACTS_SOURCE_DIR}/locales/*.ts" 
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/mediaquery/*.ts"
        # "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/registries/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/test/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/test-resources/*" # Note no *.ts
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/util/*.ts"
        "${VCPKG_ARTIFACTS_SOURCE_DIR}/yaml/*.ts"
    )
    set(VCPKG_ARTIFACTS_SOURCES ${VCPKG_ARTIFACTS_ROOT_SOURCES} ${VCPKG_ARTIFACTS_NESTED_SOURCES})
    list(TRANSFORM VCPKG_ARTIFACTS_SOURCES PREPEND "${VCPKG_ARTIFACTS_SOURCE_DIR}/")

    set(VCPKG_ARTIFACTS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-artifacts-build")
    set(VCPKG_ARTIFACTS_BINARY "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-artifacts.mjs")
    add_custom_command(
        OUTPUT "${VCPKG_ARTIFACTS_SOURCE_DIR}/locales/messages.json"
        COMMAND "${NODEJS}" ARGS "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules/translate-strings/dist/main.js" "." "--json"
        DEPENDS
            ${VCPKG_ARTIFACTS_SOURCES}
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules"
        WORKING_DIRECTORY "${VCPKG_ARTIFACTS_SOURCE_DIR}"
        COMMENT "Running artifacts translate-strings..."
        VERBATIM
    )

    add_custom_command(
        OUTPUT
            ${VCPKG_ARTIFACTS_BINARY}
        COMMAND
            "${NODEJS}" ARGS "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules/@vercel/ncc/dist/ncc/cli.js"
                build "main.ts"
                --out "${VCPKG_ARTIFACTS_BUILD_DIR}"
        COMMAND
            "${CMAKE_COMMAND}" ARGS -E copy "${VCPKG_ARTIFACTS_BUILD_DIR}/index.js" "${VCPKG_ARTIFACTS_BINARY}"
        WORKING_DIRECTORY
            "${VCPKG_ARTIFACTS_SOURCE_DIR}"
        DEPENDS
            npm-restore
            ${VCPKG_ARTIFACTS_SOURCES}
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/tsconfig.json"
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/package.json"
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/package-lock.json"
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/.npmrc"
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/.mocharc.json"
            "${VCPKG_ARTIFACTS_SOURCE_DIR}/locales/messages.json"
        COMMENT
            "Building vcpkg-artifacts..."
        VERBATIM
        )

    add_custom_target(vcpkg-artifacts-target ALL DEPENDS "${VCPKG_ARTIFACTS_BINARY}")
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
        Threads::Threads
    PRIVATE
        ${CPP_ATOMIC_LIBRARY}
)

if(CMAKE_VERSION GREATER_EQUAL "3.16")
    target_precompile_headers(vcpkglib PRIVATE "include/pch.h")
elseif(NOT MSVC)
    target_compile_options(vcpkglib PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    target_link_libraries(vcpkglib PRIVATE socket)
endif()

if(ANDROID AND CMAKE_SYSTEM_VERSION LESS "28")
    # pkg install libandroid-spawn
    target_link_libraries(vcpkglib PRIVATE android-spawn)
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

# === Target: vcpkg-ps1 ===
add_custom_command(
    OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-shell.ps1"
    COMMAND
        "${CMAKE_COMMAND}" ARGS -E copy "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg-in-development.ps1" "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-shell.ps1"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg-in-development.ps1"
    VERBATIM
)
add_custom_target(vcpkg-ps1 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/vcpkg-shell.ps1")

# === Target: vcpkg ===

add_executable(vcpkg ${VCPKG_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
target_link_libraries(vcpkg PRIVATE vcpkglib ${CMAKE_DL_LIBS})

if(VCPKG_ADD_SOURCELINK)
    if(VCPKG_VERSION STREQUAL "unknownhash")
        message(FATAL_ERROR "Attempted to add source link information, but there was no git SHA defined. VCPKG_ADD_SOURCELINK only works if VCPKG_EMBED_GIT_SHA is set.")
    endif()
    vcpkg_target_add_sourcelink(vcpkg
        REPO "microsoft/vcpkg-tool"
        REF "${VCPKG_VERSION}"
    )
endif()

set_property(TARGET vcpkg PROPERTY PDB_NAME "vcpkg${VCPKG_PDB_SUFFIX}")

# === Target: generate-message-map ===
set(GENERATE_MESSAGE_MAP_DEPENDENCIES vcpkg)
if (VCPKG_ARTIFACTS_DEVELOPMENT)
    list(APPEND GENERATE_MESSAGE_MAP_DEPENDENCIES "${VCPKG_ARTIFACTS_SOURCE_DIR}/locales/messages.json")
endif()

add_custom_target(generate-message-map
    COMMAND "$<TARGET_FILE:vcpkg>" z-generate-default-message-map locales/messages.json "${VCPKG_ARTIFACTS_SOURCE_DIR}/locales/messages.json"
    DEPENDS ${GENERATE_MESSAGE_MAP_DEPENDENCIES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
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
    set_property(TARGET vcpkg-test PROPERTY PDB_NAME "vcpkg-test${VCPKG_PDB_SUFFIX}")
    if(ANDROID)
        target_link_libraries(vcpkg-test PRIVATE log)
    endif()

    add_dependencies(vcpkg-test reads-stdin closes-exit-minus-one closes-stdin closes-stdout test-editor)

    if(CMAKE_VERSION GREATER_EQUAL "3.16")
        target_precompile_headers(vcpkg-test REUSE_FROM vcpkglib)
    elseif(NOT MSVC)
       target_compile_options(vcpkg-test PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h")
    endif()

    add_test(NAME vcpkg-test COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C "$<TARGET_FILE:vcpkg-test>" --order rand --rng-seed time)
    if(VCPKG_BUILD_BENCHMARKING)
        target_compile_options(vcpkg-test PRIVATE -DCATCH_CONFIG_ENABLE_BENCHMARKING)
    endif()
    if(VCPKG_ARTIFACTS_DEVELOPMENT)
        add_test(NAME artifacts COMMAND "${NODEJS}" "${VCPKG_ARTIFACTS_SOURCE_DIR}/node_modules/mocha/bin/mocha.js" "--import=tsx" WORKING_DIRECTORY "${VCPKG_ARTIFACTS_SOURCE_DIR}")
    endif()
endif()

# === Target: vcpkg-fuzz ===
if(VCPKG_BUILD_FUZZING)
    add_executable(vcpkg-fuzz ${VCPKG_FUZZ_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
    target_link_libraries(vcpkg-fuzz PRIVATE vcpkglib)
    set_property(TARGET vcpkg-fuzz PROPERTY PDB_NAME "vcpkg-fuzz${VCPKG_PDB_SUFFIX}")
endif()


# === Target: tls12-download ===

if(VCPKG_BUILD_TLS12_DOWNLOADER)
    add_executable(tls12-download ${TLS12_DOWNLOAD_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
    set_property(TARGET tls12-download PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET tls12-download APPEND PROPERTY LINK_OPTIONS "$<IF:$<CONFIG:Debug>,,/ENTRY:entry>")
    target_link_libraries(tls12-download winhttp wintrust shell32)
    set_property(TARGET tls12-download PROPERTY PDB_NAME "tls12-download${VCPKG_PDB_SUFFIX}")
endif()

if (BUILD_TESTING)
# === Target: closes-exit-minus-one ===

add_executable(closes-exit-minus-one ${CLOSES_EXIT_MINUS_ONE_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET closes-exit-minus-one PROPERTY PDB_NAME "closes-exit-minus-one${VCPKG_PDB_SUFFIX}")

# === Target: closes-stdin ===

add_executable(closes-stdin ${CLOSES_STDIN_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET closes-stdin PROPERTY PDB_NAME "closes-stdin${VCPKG_PDB_SUFFIX}")

# === Target: closes-stdout ===

add_executable(closes-stdout ${CLOSES_STDOUT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET closes-stdout PROPERTY PDB_NAME "closes-stdout${VCPKG_PDB_SUFFIX}")

# === Target: reads-stdin ===

add_executable(reads-stdin ${READS_STDIN_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET reads-stdin PROPERTY PDB_NAME "reads-stdin${VCPKG_PDB_SUFFIX}")

# === Target: test-editor ===

add_executable(test-editor ${TEST_EDITOR_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET test-editor PROPERTY PDB_NAME "test-editor${VCPKG_PDB_SUFFIX}")

# === Target: test-editor ===

add_executable(test-script-asset-cache ${TEST_SCRIPT_ASSET_CACHE_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/src/vcpkg.manifest")
set_property(TARGET test-script-asset-cache PROPERTY PDB_NAME "test-script-asset-cache${VCPKG_PDB_SUFFIX}")
endif()

# === Target: verify_messages ===
add_custom_target(
    verify-messages
    COMMAND pwsh -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verifyMessages.ps1"
    COMMENT "Running PowerShell script to verify message usage..."
)

# === Target: format ===

find_program(CLANG_FORMAT clang-format PATHS "$ENV{PROGRAMFILES}/LLVM/bin")
if(CLANG_FORMAT)
    # doing all of these formats in one line has a tendency to overflow the command line length
    add_custom_target(format
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_BASE_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_BASE_INCLUDES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKGLIB_INCLUDES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_SOURCES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_TEST_SOURCES}
        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_TEST_INCLUDES}

        COMMAND "${CLANG_FORMAT}" -i -verbose ${VCPKG_FUZZ_SOURCES} ${TLS12_DOWNLOAD_SOURCES}
            ${CLOSES_STDIN_SOURCES} ${CLOSES_STDOUT_SOURCES} ${READS_STDIN_SOURCES} ${CLOSES_EXIT_MINUS_ONE_SOURCES}
            ${TEST_EDITOR_SOURCES} ${TEST_SCRIPT_ASSET_CACHE_SOURCES}
    )
endif()

# ====================
# === Installation ===
# ====================

install(TARGETS vcpkg
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
