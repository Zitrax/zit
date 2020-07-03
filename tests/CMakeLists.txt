option(COVERAGE "Enable coverage of unit tests" OFF)
option(INTEGRATION_TESTS "Enable integration tests" OFF)

find_package(Threads)

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
  test_integrate.cpp
  test_torrent.cpp
  test_main.cpp
  test_net.cpp
  test_peer.cpp
  test_sha1.cpp
  test_string_utils.cpp
  test_types.cpp
  ../src/bitfield.cpp
  ../src/file_writer.cpp
  ../src/torrent.cpp
  ../src/net.cpp
  ../src/peer.cpp
  ../src/sha1.cpp
  ../src/messages.cpp
  ../src/piece.cpp)
target_link_libraries(zitest
  gtest
  ${OPENSSL_SSL_LIBRARIES}
  ${OPENSSL_CRYPTO_LIBRARIES}
  Threads::Threads
  ${CMAKE_DL_LIBS}
  tbb
)
if(NOT WIN32)
  if(SANITIZED_LIBCXX)
    target_link_libraries(zitest c++fs)
  else()
    target_link_libraries(zitest stdc++fs)
  endif()
endif()
add_test(NAME zitest COMMAND zitest)

if(INTEGRATION_TESTS)
  target_compile_definitions(zitest PUBLIC INTEGRATION_TESTS)
endif()

target_compile_definitions(zitest PUBLIC DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# For getting backtraces use:
# sudo ln -s /usr/bin/llvm-symbolizer-3.8 /usr/bin/llvm-symbolizer
# export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
# export ASAN_OPTIONS=symbolize=1
add_executable(zit_libfuzzer EXCLUDE_FROM_ALL main_libfuzzer.cpp)
target_compile_options(zit_libfuzzer PUBLIC -fsanitize=address,fuzzer)
set_target_properties(zit_libfuzzer PROPERTIES LINK_FLAGS -fsanitize=address,fuzzer)
# TODO: Better - but only in CMake >= 3.13 ( travis is on 3.12 )
# target_link_options(zit_libfuzzer PUBLIC -fsanitize=address,fuzzer)
