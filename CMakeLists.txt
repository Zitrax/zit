cmake_minimum_required (VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
project(zit CXX)

add_subdirectory(src)
add_subdirectory(tests)

enable_testing()
