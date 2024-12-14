// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "types.hpp"

namespace zit {

/**
 * Extract string from bytes vector.
 *
 * start and end specify the range [start, end)
 */
inline std::string from_bytes(const bytes& buffer,
                              std::string::size_type start = 0,
                              std::string::size_type end = 0) {
  using namespace std::string_literals;
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
  return {
      reinterpret_cast<const char*>(&buffer[start]),
      reinterpret_cast<const char*>(&buffer[end == 0 ? buffer.size() : end])};
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

/**
 * Convert bytes to hexadecimal byte representation.
 */
inline std::string to_hex(const bytes& data) {
  std::stringstream ss;
  ss << std::setfill('0') << std::hex << std::uppercase;

  for (auto b : data) {
    ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(b));
  }

  return ss.str();
}

// trim functions below from https://stackoverflow.com/a/217605/11722

// trim from start (in place)
inline void ltrim(std::string& s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));
}

// trim from end (in place)
inline void rtrim(std::string& s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

// trim from both ends (in place)
inline void trim(std::string& s) {
  ltrim(s);
  rtrim(s);
}

// trim from start (copying)
inline std::string ltrim_copy(std::string s) {
  ltrim(s);
  return s;
}

// trim from end (copying)
inline std::string rtrim_copy(std::string s) {
  rtrim(s);
  return s;
}

// trim from both ends (copying)
inline std::string trim_copy(std::string s) {
  trim(s);
  return s;
}

inline std::string to_lower(const std::string& s) {
  std::string res;
  res.resize(s.size());
  std::ranges::transform(s, res.begin(), ::tolower);
  return res;
}

// String split from https://stackoverflow.com/a/64886763/11722
inline std::vector<std::string>
split(  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const std::string& str,
    const std::string& regex_str) {
  const std::regex regexz(regex_str);
  return {std::sregex_token_iterator(str.begin(), str.end(), regexz, -1),
          std::sregex_token_iterator()};
}

/**
 * Convert to human readable units (using base 2), i.e. KiB not KB.
 *
 * @param bytes Number of bytes to convert
 */
inline std::string bytesToHumanReadable(int64_t bytes) {
  const std::vector<std::tuple<int64_t, std::string>> limits{{1LL << 40, "TiB"},
                                                             {1LL << 30, "GiB"},
                                                             {1LL << 20, "MiB"},
                                                             {1LL << 10, "KiB"},
                                                             {0, "B"}};
  const auto abytes = std::abs(bytes);
  const auto* sign = (bytes < 0 ? "-" : "");
  for (const std::tuple<int64_t, std::string>& limit_unit : limits) {
    const int64_t& limit = std::get<0>(limit_unit);
    const std::string& unit = std::get<1>(limit_unit);
    if (abytes >= limit) {
      if (limit) {
        const auto w = abytes / limit;
        const auto f =
            static_cast<float>(abytes - w * limit) / static_cast<float>(limit);
        return fmt::format("{}{:.2f} {}", sign, static_cast<float>(w) + f,
                           unit);
      }
      return fmt::format("{}{} {}", sign, abytes, unit);
    }
  }
  throw std::runtime_error("Bogus byte conversion");
}

}  // namespace zit
