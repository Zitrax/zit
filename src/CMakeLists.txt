add_compile_options(-Wall -Wextra -Werror -Weffc++ -Wconversion -Wsign-conversion)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

add_executable(zit main.cpp torrent.cpp net.cpp sha1.cpp peer.cpp bitfield.cpp messages.cpp)

# Enable Link Time Optimization for release builds if supported
# (Note: clang can even use -flto=full, but has to be manually
#        added - no cmake support for that)
if(CMAKE_BUILD_TYPE MATCHES Release)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo OUTPUT output)
  if(ipo)
    message(STATUS "LTO Enabled")
    set_property(TARGET zit PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

  # Strip binary in release mode
  add_custom_command(TARGET zit POST_BUILD
    COMMENT "Stripping zit"
    COMMAND ${CMAKE_STRIP} zit
  )
endif()

# Note this needs to be compiled with afl-g++ or afl-clang++
add_executable(zit_afl EXCLUDE_FROM_ALL main_afl.cpp)
target_link_libraries(zit pthread crypto)
if(SANITIZED_LIBCXX)
  target_link_libraries(zit c++fs)
else()
  target_link_libraries(zit stdc++fs)
endif()
