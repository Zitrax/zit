option(COVERAGE "Enable coverage of unit tests" OFF)
option(INTEGRATION_TESTS "Enable integration tests" OFF)
option(TESTS_ASAN "Enable address sanitizer" OFF)

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

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

# The test code
include_directories("../src")
add_executable(zitest
  test_arg_parser.cpp
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
  $<TARGET_OBJECTS:zitlib>)

target_link_libraries(zitest
  Threads::Threads
  CONAN_PKG::openssl
  CONAN_PKG::asio
  CONAN_PKG::tbb
  CONAN_PKG::gtest
  CONAN_PKG::spdlog
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

if(TESTS_ASAN)
  target_compile_options(zitest PRIVATE -fsanitize=address)
  target_link_options(zitest PRIVATE -fsanitize=address)
endif()

# For getting backtraces use:
# sudo ln -s /usr/bin/llvm-symbolizer-3.8 /usr/bin/llvm-symbolizer
# export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
# export ASAN_OPTIONS=symbolize=1
add_executable(zit_libfuzzer EXCLUDE_FROM_ALL main_libfuzzer.cpp)
target_compile_options(zit_libfuzzer PUBLIC -fsanitize=address,fuzzer)
target_link_options(zit_libfuzzer PUBLIC -fsanitize=address,fuzzer)
