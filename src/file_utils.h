// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

#include <filesystem>
#include <fstream>

namespace zit {

/**
 * @param file_name
 */
inline auto read_file(const std::filesystem::path& file_name) {
  std::ifstream file_stream{file_name};
  file_stream.exceptions(std::ifstream::failbit);
  file_stream.clear();
  std::ostringstream str_stream{};
  file_stream >> str_stream.rdbuf();
  return str_stream.str();
}

}  // namespace zit
