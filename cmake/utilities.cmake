if(MSVC AND NOT COMMAND target_precompile_headers)
    message(FATAL_ERROR "CMake 3.16 (target_precompile_headers) is required to build with MSVC")
endif()

# Outputs to Cache: VCPKG_COMPILER
function(vcpkg_detect_compiler)
    if(NOT DEFINED CACHE{VCPKG_COMPILER})
        message(STATUS "Detecting the C++ compiler in use")
        if(CMAKE_COMPILER_IS_GNUXX OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6.0)
                message(FATAL_ERROR [[
The g++ version picked up is too old; please install a newer compiler such as g++-7.
On Ubuntu try the following:
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
    sudo apt-get update -y
    sudo apt-get install g++-7 -y
On CentOS try the following:
    sudo yum install centos-release-scl
    sudo yum install devtoolset-7
    scl enable devtoolset-7 bash
]])
            endif()

            set(COMPILER "gcc")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
            set(COMPILER "clang")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "[Cc]lang")
            set(COMPILER "clang")
        elseif(MSVC)
            set(COMPILER "msvc")
        else()
            message(FATAL_ERROR "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
        endif()

        set(VCPKG_COMPILER ${COMPILER}
            CACHE STRING
            "The compiler in use; one of gcc, clang, msvc")
        message(STATUS "Detecting the C++ compiler in use - ${VCPKG_COMPILER}")
    endif()
endfunction()

function(vcpkg_target_add_sourcelink target)
    cmake_parse_arguments(PARSE_ARGV 1 "arg" "" "REPO;REF" "")
    if(DEFINED arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "vcpkg_cmake_buildsystem_build was passed extra arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required_arg IN ITEMS REPO REF)
        if(NOT DEFINED arg_${required_arg})
            message(FATAL_ERROR "${required_arg} must be set")
        endif()
    endforeach()

    if(MSVC)
        set(base_url "https://raw.githubusercontent.com/${REPO}/${REF}/*")
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/*" local_base)
        string(REPLACE [[\]] [[\\]] local_base "${local_base}")
        set(output_json_filename "${CMAKE_CURRENT_BINARY_DIR}/vcpkgsourcelink.json")
        # string(JSON was added in CMake 3.19, so just create the json from scratch
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vcpkgsourcelink.json"
"{
  \"documents\": {
    \"${local_base}\": \"${base_url}\"
  }
}"
        )
        file(TO_NATIVE_PATH "${output_json_filename}" native_json_filename)
        target_link_options("${target}" PRIVATE "/SOURCELINK:${native_json_filename}")
    endif()
endfunction()

function(vcpkg_define_version)
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

    set(VCPKG_BASE_VERSION "${VCPKG_BASE_VERSION}" PARENT_SCOPE)
    set(VCPKG_VERSION "${VCPKG_VERSION}" PARENT_SCOPE)
endfunction()

macro(vcpkg_setup_compiler_flags)
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
endmacro()
