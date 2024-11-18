cmake_minimum_required(VERSION 3.7.2)

project(many-targets CXX)

include(GNUInstallDirs)

install(FILES many-targets.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

set(ALL_TARGETS)

foreach(NUMBER RANGE 1 10)
    set(THIS_TARGET "vcpkg-cmake-config-many-targets-${NUMBER}")
    add_library("${THIS_TARGET}" many-targets.cpp many-targets.def many-targets.h)
    target_include_directories("${THIS_TARGET}" INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    list(APPEND ALL_TARGETS "${THIS_TARGET}")
endforeach()

install(TARGETS ${ALL_TARGETS} EXPORT cmake-config-many-targetsConfig)

install(EXPORT cmake-config-many-targetsConfig NAMESPACE cmake-config-many-targets:: DESTINATION share/cmake-config-many-targets)
