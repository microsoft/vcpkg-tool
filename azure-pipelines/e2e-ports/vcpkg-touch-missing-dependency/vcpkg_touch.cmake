include_guard(GLOBAL)

function(vcpkg_touch)
    cmake_parse_arguments(PARSE_ARGV 0 "arg" "" "TARGET" "")

    if(DEFINED arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "vcpkg_touch was passed extra arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()

    file(TOUCH "${arg_TARGET}")
endfunction()
