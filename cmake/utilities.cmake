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

function(vcpkg_target_add_warning_options TARGET)
    if(MSVC)
        # either MSVC, or clang-cl
        target_compile_options(${TARGET} PRIVATE -FC -permissive- -utf-8)

        if(VCPKG_DEVELOPMENT_WARNINGS)
            target_compile_options(${TARGET} PRIVATE -W4)
            if(VCPKG_COMPILER STREQUAL "clang")
                target_compile_options(${TARGET} PRIVATE
                    -Wmissing-prototypes
                    -Wno-missing-field-initializers
                    )
            else()
                target_compile_options(${TARGET} PRIVATE -analyze -analyze:stacksize 39000)
            endif()
        endif()

        if(VCPKG_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET} PRIVATE -WX)
        endif()
    else()
        if(VCPKG_DEVELOPMENT_WARNINGS)
            target_compile_options(${TARGET} PRIVATE
                -Wall -Wextra -Wpedantic
                -Wno-unknown-pragmas
                -Wno-missing-field-initializers
                -Wno-redundant-move
                )

            # GCC and clang have different names for the same warning
            if(VCPKG_COMPILER STREQUAL "gcc")
                target_compile_options(${TARGET} PRIVATE
                    -Wmissing-declarations
                    )
            elseif(VCPKG_COMPILER STREQUAL "clang")
                target_compile_options(${TARGET} PRIVATE
                    -Wmissing-prototypes
                    -Wno-range-loop-analysis
                    )
            endif()
        endif()

        if(VCPKG_WARNINGS_AS_ERRORS)
            target_compile_options(${TARGET} PRIVATE -Werror)
        endif()
    endif()
    if(APPLE)
        target_link_libraries(${TARGET} PRIVATE "-framework CoreFoundation" "-framework ApplicationServices")
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
