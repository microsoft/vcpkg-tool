cmake_minimum_required(VERSION 3.7.2)

project(hello-world-2 C)

include(GNUInstallDirs)

add_library(hello-world-2 hello.c hello.def hello-2.h)

install(FILES hello-2.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
target_include_directories(hello-world-2 INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS hello-world-2 EXPORT hello-world-2Config)

install(EXPORT hello-world-2Config NAMESPACE hello-world-2:: DESTINATION share/hello-world-2)
