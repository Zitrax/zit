add_compile_options(-Wall -Wextra -Werror -Weffc++)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

add_executable(zit main.cpp torrent.cpp net.cpp sha1.cpp)

# Note this needs to be compiled with afl-g+ or afl-clang++
add_executable(zit_afl EXCLUDE_FROM_ALL main_afl.cpp)
target_link_libraries(zit stdc++fs pthread crypto)
