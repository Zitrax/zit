enable_testing()

option(COVERAGE "Enable coverage of unit tests" OFF)

# Download and unpack googletest at configure time
configure_file(CMakeLists.txt.in googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  OUTPUT_QUIET
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  OUTPUT_QUIET
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
                 ${CMAKE_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)

# The gtest/gtest_main targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
  include_directories("${gtest_SOURCE_DIR}/include")
endif()

if(COVERAGE)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    include(CodeCoverage)
    append_coverage_compiler_flags()
    setup_target_for_coverage_lcov(
      NAME zitest_coverage
      EXECUTABLE zitest
      DEPENDENCIES zitest
      )
  endif()
endif()

# Should optimally share these with the src (but not pass to gtest)
if(UNIX)
  add_compile_options(-Wall -Wextra -Werror -Weffc++ -Wconversion -Wsign-conversion)
  # Test specific
  add_compile_options(-Wno-unused-variable)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

# The test code
include_directories("../src")
add_executable(zitest
  test_bencode.cpp
  test_bitfield.cpp
  test_torrent.cpp
  test_net.cpp
  test_peer.cpp
  test_string_utils.cpp
  test_types.cpp
  ../src/bitfield.cpp
  ../src/torrent.cpp
  ../src/net.cpp
  ../src/peer.cpp
  ../src/sha1.cpp
  ../src/messages.cpp
  ../src/piece.cpp)
target_link_libraries(zitest gtest gtest_main ${OPENSSL_CRYPTO_LIBRARY})
if(NOT WIN32)
  if(SANITIZED_LIBCXX)
    target_link_libraries(zitest c++fs)
  else()
    target_link_libraries(zitest stdc++fs)
  endif()
endif()
add_test(NAME zitest COMMAND zitest)
