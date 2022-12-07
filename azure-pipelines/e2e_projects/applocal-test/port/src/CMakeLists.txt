cmake_minimum_required(VERSION 3.7.2)

project(mylib CXX)

include(GNUInstallDirs)

add_library(mylib mylib.cxx mylib.h)

install(FILES mylib.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
target_include_directories(mylib INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS mylib EXPORT mylibConfig)

install(EXPORT mylibConfig NAMESPACE mylib:: DESTINATION share/mylib)
