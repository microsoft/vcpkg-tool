cmake_minimum_required(VERSION 3.15) # 3.15 for MSVC_RUNTIME_LIBRARY, removing default warning flags

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0092 NEW)
if(NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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

    set(VCPKG_BASE_VERSION "9999-99-99")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /guard:cf")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG /debugtype:cv,fixup /guard:cf")
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

# ===============
# === Targets ===
# ===============

# === Target: vcpkglib ===

add_library(vcpkglib OBJECT
    ${VCPKGLIB_BASE_SOURCES}
    ${VCPKGLIB_SOURCES}
    ${VCPKGLIB_BASE_INCLUDES}
    ${VCPKGLIB_INCLUDES}
)
target_include_directories(vcpkglib PUBLIC include)

vcpkg_target_add_warning_options(vcpkglib)
target_compile_definitions(vcpkglib PUBLIC
    VCPKG_VERSION=${VCPKG_VERSION}
    VCPKG_BASE_VERSION=${VCPKG_BASE_VERSION}
)

if(NOT DEFINED VCPKG_READONLY_BUNDLE_SHA OR VCPKG_READONLY_BUNDLE_SHA STREQUAL "")
    if(VCPKG_OFFICIAL_BUILD)
        message(FATAL_ERROR "VCPKG_READONLY_BUNDLE_SHA is required for official builds.")
    endif()
else()
    target_compile_definitions(vcpkglib PUBLIC
        VCPKG_READONLY_BUNDLE_SHA=${VCPKG_READONLY_BUNDLE_SHA}
    )
endif()

if(NOT DEFINED VCPKG_CE_SHA OR VCPKG_CE_SHA STREQUAL "")
    if(VCPKG_OFFICIAL_BUILD)
        message(FATAL_ERROR "VCPKG_CE_SHA is required for official builds.")
    endif()
else()
    target_compile_definitions(vcpkglib PUBLIC
        VCPKG_CE_SHA=${VCPKG_CE_SHA}
    )
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(vcpkglib
    PUBLIC
        fmt::fmt
    PRIVATE
        Threads::Threads
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

add_executable(vcpkg ${VCPKG_SOURCES})
target_link_libraries(vcpkg PRIVATE vcpkglib)
vcpkg_target_add_warning_options(vcpkg)
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

# === Target: vcpkg-test ===

if (BUILD_TESTING)
    enable_testing()

    add_executable(vcpkg-test
        ${VCPKG_TEST_SOURCES}
        ${VCPKG_TEST_INCLUDES}
    )
    target_link_libraries(vcpkg-test PRIVATE vcpkglib)
    vcpkg_target_add_warning_options(vcpkg-test)

    add_test(NAME default COMMAND vcpkg-test --order rand --rng-seed time)

    if (VCPKG_BUILD_BENCHMARKING)
        target_compile_options(vcpkg-test PRIVATE -DCATCH_CONFIG_ENABLE_BENCHMARKING)
    endif()
endif()

# === Target: vcpkg-fuzz ===

if(VCPKG_BUILD_FUZZING)
    add_executable(vcpkg-fuzz ${VCPKG_FUZZ_SOURCES})
    target_link_libraries(vcpkg-fuzz PRIVATE vcpkglib)
    vcpkg_target_add_warning_options(vcpkg-fuzz)
endif()


# === Target: tls12-download ===

if(VCPKG_BUILD_TLS12_DOWNLOADER)
    add_executable(tls12-download ${TLS12_DOWNLOAD_SOURCES})
    set_property(TARGET tls12-download PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET tls12-download APPEND PROPERTY LINK_OPTIONS "$<IF:$<CONFIG:Debug>,,/ENTRY:entry>")
    target_link_libraries(tls12-download winhttp wintrust shell32)
    if(VCPKG_PDB_SUFFIX)
        set_property(TARGET tls12-download PROPERTY PDB_NAME "tls12-download${VCPKG_PDB_SUFFIX}")
    endif()
endif()


# === Target: format ===

find_program(CLANG_FORMAT clang-format)
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
