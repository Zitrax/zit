cmake_minimum_required (VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
project(zit CXX)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  set(CONAN_SYSTEM_INCLUDES ON)
  conan_basic_setup()
endif()

set(SANITIZED_LIBCXX "" CACHE PATH "Path to custom sanitized libc++ install directory")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8")
    message(FATAL_ERROR "g++ 8 or higher is required")
  endif()
  # Support for clang seemed to go far back, so not bothering to check that atm.
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)

if(SANITIZED_LIBCXX)
  # Note: A memory sanitized libcxx can be built using the instructions at
  #       https://libcxx.llvm.org/docs/BuildingLibcxx.html using cmake
  #       something like:
  #         cmake .. -DCMAKE_INSTALL_PREFIX=.. -DCMAKE_BUILD_TYPE=Release -DLLVM_USE_SANITIZER=Memory
  #         make install-libcxx install-libcxxabi -j$num_of_processors
  include_directories("${SANITIZED_LIBCXX}/include/c++/v1")
  add_compile_options("$<$<CONFIG:DEBUG>:-fsanitize=memory;-stdlib=libc++>")
  # TODO: CMake 3.13 has add_link_options()
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=memory -L${SANITIZED_LIBCXX}/lib -Wl,-rpath,${SANITIZED_LIBCXX}/lib -stdlib=libc++ -lc++abi")
endif()

if(WIN32)
  find_path(ASIO_INCLUDE_DIR asio.hpp HINTS $ENV{USERPROFILE}/Source/asio-1.12.2/include)
  if(NOT ASIO_INCLUDE_DIR)
    messge(FATAL_ERROR "Need ASIO include directory")
  else()
    message(STATUS "asio.hpp found in ${ASIO_INCLUDE_DIR}")
    # Many warnings from asio regarding this using msvc
    add_compile_options(-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING)
    add_compile_options(-D_WIN32_WINNT=0x0501)
  endif()
  add_compile_options(-DASIO_STANDALONE)
  include_directories(${ASIO_INCLUDE_DIR})

  # Use default installation folder if existing
  find_path(WIN_OPENSSL_DIR sha.h HINTS $ENV{ProgramW6432}/OpenSSL-Win64/include/openssl)
  if(WIN_OPENSSL_DIR)
    set(OPENSSL_ROOT_DIR "${WIN_OPENSSL_DIR}/../..")
  endif()
endif()

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

include_directories(SYSTEM "external")

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)

file(GLOB_RECURSE ALL_SRC_SOURCE_FILES src/*.cpp)
file(GLOB_RECURSE ALL_SRC_HEADER_FILES src/*.h)

file(GLOB_RECURSE ALL_TEST_SOURCE_FILES tests/*.cpp)
file(GLOB_RECURSE ALL_TEST_HEADER_FILES tests/*.h)

set(ALL_SOURCE_FILES ${ALL_SRC_SOURCE_FILES} ${ALL_TEST_SOURCE_FILES})
set(ALL_HEADER_FILES ${ALL_SRC_HEADER_FILES} ${ALL_TEST_HEADER_FILES})

# For some reason some temporary? files are picked up
# failing the command. Filter them away.
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "#")
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX "#")
set(ALL_INPUT_FILES ${ALL_SOURCE_FILES} ${ALL_HEADER_FILES})

find_program(CLANG_FORMAT
  NAMES clang-format clang-format-5.0)

if(CLANG_FORMAT)
  add_custom_target(format
    COMMENT "Formatting ${ALL_INPUT_FILES}"
    COMMAND ${CLANG_FORMAT} -style=Chromium -i ${ALL_INPUT_FILES}
    )
else()
  message(WARNING "Could not find clang-format, formatting not available")
endif()

find_program(CLANG_TIDY run-clang-tidy)

if(CLANG_TIDY)
  if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(FATAL_ERROR "Need to pass -DCMAKE_EXPORT_COMPILE_COMMANDS=ON to run clang-tidy")
  endif()

  # For now running this only on the src/ files since the googletest
  # macros generate tons of issues and clang-tidy has no good mechanism
  # of filtering that out. See https://reviews.llvm.org/D26418
  add_custom_target(tidy
    COMMENT "Tidying ${ALL_SRC_SOURCE_FILES}"
    COMMAND ${CLANG_TIDY}
            -quiet
            -p ${CMAKE_CURRENT_BINARY_DIR}
            ${ALL_SRC_SOURCE_FILES}
    )

  add_custom_target(tidy_fix
    COMMENT "Tidying ${ALL_SRC_SOURCE_FILES}"
    COMMAND ${CLANG_TIDY}
            -quiet -fix
            -p ${CMAKE_CURRENT_BINARY_DIR}
            ${ALL_SRC_SOURCE_FILES}
    )
else()
    message(WARNING "Could not find clang-tidy, tidy checking not available")
endif()
