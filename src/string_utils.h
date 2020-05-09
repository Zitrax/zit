// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <iomanip>
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

  // Visual Studio 2019 crashes using the string constructor
  // below; thus the iterator loop instead.

#ifdef WIN32
  std::stringstream ss;
  auto it_end = buffer.cbegin() + (end == 0 ? buffer.size() : end);
  for (auto it = buffer.cbegin() + start; it != it_end; ++it) {
    ss << static_cast<char>(*it);
  }
  return ss.str();
#else
  return std::string(
      reinterpret_cast<const char*>(&buffer[start]),
      reinterpret_cast<const char*>(&buffer[end == 0 ? buffer.size() : end]));
#endif  // WIN32
}

/**
 * Convert string to hexadecimal byte representation.
 */
inline std::string to_hex(const std::string& str) {
  std::stringstream ss;
  ss << std::setfill('0') << std::hex << std::uppercase;

  for (auto ch : str) {
    ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(ch));
  }

  return ss.str();
}

// trim functions below from https://stackoverflow.com/a/217605/11722

/** trim from start (in place) */
inline void ltrim(std::string& s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))));
}

/** trim from end (in place) */
inline void rtrim(std::string& s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       std::not1(std::ptr_fun<int, int>(std::isspace)))
              .base(),
          s.end());
}

/** trim from both ends (in place) */
inline void trim(std::string& s) {
  ltrim(s);
  rtrim(s);
}

/** trim from start (copying) */
inline std::string ltrim_copy(std::string s) {
  ltrim(s);
  return s;
}

/** trim from end (copying) */
inline std::string rtrim_copy(std::string s) {
  rtrim(s);
  return s;
}

}  // namespace zit
