// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>  // uint8_t
#include <limits>
#include <stdexcept>  // out_of_range
#include <vector>

#include <iostream>

namespace zit {

// Type aliases
using bytes = std::vector<std::byte>;

// numeric_cast ( Copied from https://codereview.stackexchange.com/a/26496/39248
// ) by user Matt Whitlock - functions renamed.

template <typename I, typename J>
static typename std::
    enable_if<std::is_signed<I>::value && std::is_signed<J>::value, I>::type
    numeric_cast(J value) {
  if (value < std::numeric_limits<I>::min() ||
      value > std::numeric_limits<I>::max()) {
    throw std::out_of_range("out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static typename std::
    enable_if<std::is_signed<I>::value && std::is_unsigned<J>::value, I>::type
    numeric_cast(J value) {
  if (value > static_cast<typename std::make_unsigned<I>::type>(
                  std::numeric_limits<I>::max())) {
    throw std::out_of_range("out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static typename std::
    enable_if<std::is_unsigned<I>::value && std::is_signed<J>::value, I>::type
    numeric_cast(J value) {
  if (value < 0 || static_cast<typename std::make_unsigned<J>::type>(value) >
                       std::numeric_limits<I>::max()) {
    throw std::out_of_range("out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static typename std::
    enable_if<std::is_unsigned<I>::value && std::is_unsigned<J>::value, I>::type
    numeric_cast(J value) {
  if (value > std::numeric_limits<I>::max()) {
    throw std::out_of_range("out of range");
  }
  return static_cast<I>(value);
}

// Convenience functions

/**
 * Convenience for literal byte values.
 */
static constexpr std::byte operator"" _b(unsigned long long arg) {
  // std::byte is not an arithmetic type so we can't check min and max of it.
  // Still we want to ensure that the input fits in one byte, so using uint8_t
  // instead for the check.
  static_assert(sizeof(uint8_t) == sizeof(std::byte));
  if (arg > std::numeric_limits<uint8_t>::max() ||
      arg < std::numeric_limits<uint8_t>::min()) {
    throw std::out_of_range("Value " + std::to_string(arg) +
                            " not in the range of a byte");
  }
  return static_cast<std::byte>(arg);
}
/**
 * Convenience for literal byte values.
 */
static constexpr std::byte operator"" _b(char arg) noexcept {
  return static_cast<std::byte>(arg);
}

static inline uint32_t big_endian(const bytes& buf,
                                  bytes::size_type offset = 0) {
  if (offset + 4 > buf.size() ||
      offset > std::numeric_limits<bytes::size_type>::max() - 4) {
    throw std::out_of_range(
        "Target range outside of buffer: " + std::to_string(offset) + "," +
        std::to_string(buf.size()));
  }
  return static_cast<uint32_t>(static_cast<uint8_t>(buf[offset + 3]) << 0 |
                               static_cast<uint8_t>(buf[offset + 2]) << 8 |
                               static_cast<uint8_t>(buf[offset + 1]) << 16 |
                               static_cast<uint8_t>(buf[offset + 0]) << 24);
}

}  // namespace zit
