cmake_minimum_required(VERSION 3.15)

project(main CXX)

find_package(mylib REQUIRED)

add_executable(main main.cxx)
target_compile_features(main PRIVATE cxx_std_17)

target_link_libraries(main
  PRIVATE
    mylib::mylib)