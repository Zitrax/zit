add_compile_options(-Wall -Wextra -Werror -Weffc++)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  add_compile_options(-Wno-non-virtual-dtor)
endif()

add_executable(zit main.cpp)

# Note this needs to be compiled with afl-g+ or afl-clang++
add_executable(zit_afl EXCLUDE_FROM_ALL main_afl.cpp)

find_program(CLANG_FORMAT
  NAMES clang-format clang-format-5.0)

if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_SOURCE_FILES *.cpp *.h)
  # For some reason some temporary? files are picked up
  # failing the command. Filter them away.
  list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "#")

  add_custom_target(format
    COMMENT "Formatting ${ALL_SOURCE_FILES}"
    COMMAND ${CLANG_FORMAT} -style=Chromium -i ${ALL_SOURCE_FILES}
    )
else()
  message(WARNING "Could not find clang-format, formatting not available")
endif()
