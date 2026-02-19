cmake_minimum_required(VERSION 3.7.2)

project(broken-symlink CXX)

include(GNUInstallDirs)

add_library(broken-symlink hello.cpp hello.def hello-broken-symlink.h)

install(FILES hello-broken-symlink.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
target_include_directories(broken-symlink INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS broken-symlink EXPORT broken-symlinkConfig)

install(EXPORT broken-symlinkConfig NAMESPACE broken-symlink:: DESTINATION share/broken-symlink)
