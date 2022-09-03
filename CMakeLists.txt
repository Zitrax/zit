cmake_minimum_required (VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
project(zit CXX)

option(USE_CLANG_TIDY "Use clang-tidy when building" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  set(CONAN_SYSTEM_INCLUDES ON)
  conan_basic_setup(TARGETS)
  message(STATUS "Conan libs: ${CONAN_LIBS}")
endif()

set(SANITIZED_LIBCXX "" CACHE PATH "Path to custom sanitized libc++ install directory")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8")
    message(FATAL_ERROR "g++ 8 or higher is required")
  endif()
  # Support for clang seemed to go far back, so not bothering to check that atm.
endif()

option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options (-fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       add_compile_options (-fcolor-diagnostics)
    endif ()
endif ()

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

include_directories(SYSTEM "external")

if(UNIX)
  add_compile_options(-Wall -Wextra -Werror -Weffc++ -Wconversion -Wsign-conversion)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Be strict and just disable unwanted or noisy warnings
    add_compile_options(
      -Weverything
      -Wno-c++98-compat                   # Don't care about c++98
      -Wno-c++98-compat-pedantic          # Don't care about c++98
      -Wno-padded                         #
      -Wno-weak-vtables                   #
      -Wno-global-constructors            # performance startup-time
      -Wno-exit-time-destructors          # similar as ^ for destructors
      -Wno-undefined-func-template        # does not seem very important
      -Wno-zero-as-null-pointer-constant  # fails clang-10 on travis but not locally?
      -Wno-disabled-macro-expansion       # Does not work when interfacing with csignal
    )
  endif()
endif()

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

find_program(CLANG_FORMAT NAMES clang-format-10)
if(CLANG_FORMAT)
  add_custom_target(format
    COMMENT "Formatting ${ALL_INPUT_FILES}"
    COMMAND ${CLANG_FORMAT} -style=Chromium -i ${ALL_INPUT_FILES}
    )
else()
  message(WARNING "Could not find clang-format, formatting not available")
endif()
