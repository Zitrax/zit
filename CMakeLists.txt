cmake_minimum_required (VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
project(zit CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8")
    message(FATAL_ERROR "g++ 8 or higher is required")
  endif()
  # Support for clang seemed to go far back, so not bothering to check that atm.
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "")

add_subdirectory(src)
add_subdirectory(tests)

enable_testing()

file(GLOB_RECURSE ALL_SOURCE_FILES src/*.cpp tests/*.cpp)
file(GLOB_RECURSE ALL_HEADER_FILES src/*.h tests/*.h)
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
  set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

  if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(WARNING "Need to pass -DCMAKE_EXPORT_COMPILE_COMMANDS to run clang-tidy")
  endif()

  add_custom_target(tidy
    COMMENT "Tidying ${ALL_SOURCE_FILES}"
    COMMAND ${CLANG_TIDY} -header-filter=.*
            -p ${CMAKE_CURRENT_BINARY_DIR}
            ${ALL_SOURCE_FILES}
    )
else()
    message(WARNING "Could not find clang-tidy, tidy checking not available")
endif()
