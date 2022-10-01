cmake_minimum_required(VERSION 3.7.2)

project(hello-world-1 CXX)

include(GNUInstallDirs)

add_library(hello-world-1 hello.cpp hello.def hello-1.h)

install(FILES hello-1.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
target_include_directories(hello-world-1 INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS hello-world-1 EXPORT hello-world-1Config)

install(EXPORT hello-world-1Config NAMESPACE hello-world-1:: DESTINATION share/hello-world-1)
