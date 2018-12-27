// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <limits>
#include <vector>

// Type aliases
using bytes = std::vector<std::byte>;

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
