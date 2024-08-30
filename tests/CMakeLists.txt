option(COVERAGE "Enable coverage of unit tests" OFF)
option(INTEGRATION_TESTS "Enable integration tests" OFF)

find_package(Threads)
find_package(GTest)

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
  process.cpp
  test_arg_parser.cpp
  test_bencode.cpp
  test_bitfield.cpp
  test_global_config.cpp
  test_integrate.cpp
  test_torrent.cpp
  test_main.cpp
  test_net.cpp
  test_peer.cpp
  test_random.cpp
  test_retry.cpp
  test_sha1.cpp
  test_string_utils.cpp
  test_types.cpp
  $<TARGET_OBJECTS:zitlib>)

target_link_libraries(zitest
  asio::asio
  GTest::gtest
  GTest::gmock
  OpenSSL::SSL
  spdlog::spdlog
  TBB::tbb
  Threads::Threads
  $<$<PLATFORM_ID:Windows>:Crypt32.lib>
)

if(NOT "${CMAKE_CXX_COMPILER}" MATCHES "ccache" AND NOT "${CMAKE_CXX_COMPILER_LAUNCHER}" MATCHES "ccache")
  target_precompile_headers(zitest REUSE_FROM zitlib)
endif()

if(NOT WIN32)
  if(SANITIZED_LIBCXX)
    target_link_libraries(zitest c++fs)
  else()
    target_link_libraries(zitest stdc++fs)
  endif()
endif()

gtest_discover_tests(zitest
  EXTRA_ARGS --gtest_color=yes
  TEST_LIST allTests
)

# configure_file("CTestCustom.cmake.in" "${CMAKE_BINARY_DIR}/CTestCustom.cmake")
set_property(DIRECTORY APPEND PROPERTY
  TEST_INCLUDE_FILES ${CMAKE_CURRENT_LIST_DIR}/customTestManip.cmake
)

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
target_link_options(zit_libfuzzer PUBLIC -fsanitize=address,fuzzer)
