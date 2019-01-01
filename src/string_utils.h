// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

#include "types.h"

using namespace std::string_literals;

namespace zit {

/**
 * Extract string from bytes vector.
 *
 * start and end specify the range [start, end)
 */
inline std::string from_bytes(const bytes& buffer,
                              std::string::size_type start = 0,
                              std::string::size_type end = 0) {
  if (end > 0 && end < start) {
    throw std::invalid_argument(__FUNCTION__ + ": end < start"s);
  }
  if (end > buffer.size()) {
    throw std::invalid_argument(__FUNCTION__ + ": end > size"s);
  }
  return std::string(
      reinterpret_cast<const char*>(&buffer[start]),
      reinterpret_cast<const char*>(&buffer[end == 0 ? buffer.size() : end]));
}
}  // namespace zit
