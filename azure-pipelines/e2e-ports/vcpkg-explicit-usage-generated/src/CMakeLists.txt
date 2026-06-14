cmake_minimum_required(VERSION 3.7.2)

project(explicit-usage-generated CXX)

include(GNUInstallDirs)

add_library(explicit-usage-generated hello.cpp)

install(FILES hello.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
target_include_directories(explicit-usage-generated INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS explicit-usage-generated EXPORT explicit-usage-generatedConfig)
install(EXPORT explicit-usage-generatedConfig NAMESPACE explicit-usage-generated:: DESTINATION share/explicit-usage-generated)
