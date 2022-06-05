option(USE_CLANG_TIDY "Use clang-tidy if available" ON)
option(ASIO_DEBUG "Add asio debug output" OFF)

# Clang tidy only in the src dir since it can't run on the gtest sources
# used in the tests folder. Not possible yet to exclude only
# those headers at the moment, see https://reviews.llvm.org/D26418.
#
# Using this is costly in compile time though. Using 6 cores
# the compile time went from 16s to 48s.
#
if(USE_CLANG_TIDY)
  find_program(CLANG_TIDY NAMES clang-tidy-12 clang-tidy-10)

  if(CLANG_TIDY)
    message(STATUS "Enabling clang-tidy: ${CLANG_TIDY}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY}")
  else()
      message(WARNING "Could not find clang-tidy, tidy checking not available")
  endif()
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

# Debug asio handlers to stderr
# add_compile_options(-DASIO_ENABLE_HANDLER_TRACKING)

add_library(zitlib OBJECT
  torrent.cpp
  net.cpp
  sha1.cpp
  peer.cpp
  bitfield.cpp
  messages.cpp
  piece.cpp
  file_writer.cpp
  arg_parser.cpp
)
add_executable(zit main.cpp $<TARGET_OBJECTS:zitlib>)
target_link_libraries(zitlib PRIVATE CONAN_PKG::spdlog CONAN_PKG::asio)

if(ASIO_DEBUG)
  target_compile_definitions(zitlib PRIVATE ASIO_ENABLE_HANDLER_TRACKING)
endif()

install(TARGETS zit RUNTIME DESTINATION bin)

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
    COMMAND ${CMAKE_STRIP} $<TARGET_FILE:zit>
  )
endif()

add_custom_target(doc COMMAND doxygen WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Note this needs to be compiled with afl-g++ or afl-clang++
add_executable(zit_afl EXCLUDE_FROM_ALL main_afl.cpp)
target_link_libraries(zit
  ${OPENSSL_SSL_LIBRARIES}
  ${OPENSSL_CRYPTO_LIBRARIES}
  CONAN_PKG::asio
  CONAN_PKG::tbb
  CONAN_PKG::spdlog
)

if(NOT WIN32)
  target_link_libraries(zit pthread)
  if(SANITIZED_LIBCXX)
    target_link_libraries(zit c++fs)
  else()
    target_link_libraries(zit stdc++fs)
  endif()
endif()
