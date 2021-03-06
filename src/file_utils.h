// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <filesystem>
#include <fstream>

namespace zit {

/**
 * Read a file to a string.
 *
 * @param file_name path to file to read
 *
 * @throws if the file could not be read. Note that it also throws if the file
 *   is empty.
 */
inline auto read_file(const std::filesystem::path& file_name) {
  try {
    // Important to open in binary mode due to line endings
    // differing on windows and linux.
    std::ifstream file_stream{file_name,
                              std::ios_base::in | std::ios_base::binary};
    file_stream.exceptions(std::ifstream::failbit);
    std::ostringstream str_stream{};
    file_stream >> str_stream.rdbuf();
    return str_stream.str();
  } catch (const std::exception&) {
    std::throw_with_nested(
        std::runtime_error("Could not read: " + file_name.string()));
  }
}

}  // namespace zit
