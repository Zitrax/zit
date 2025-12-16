// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <spdlog/fmt/fmt.h>
#include <asio.hpp>
#include <cstdint>  // uint8_t
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>  // out_of_range
#include <vector>
#include "strong_type.hpp"

using asio::detail::socket_ops::host_to_network_long;

namespace zit {

// Type aliases
using bytes = std::vector<std::byte>;
using bytes_span = const std::span<const std::byte>;

// String types
using ListeningPort = StrongType<unsigned short, struct ListeningPortTag>;
using ConnectionPort = StrongType<unsigned short, struct ConnectionPortTag>;

// numeric_cast ( Copied from https://codereview.stackexchange.com/a/26496/39248
// ) by user Matt Whitlock - functions renamed and modified.

// FIXME: These casts could fall back to no checking at all if I == J

template <typename I, typename J>
static constexpr I numeric_cast(J value, const char* msg = nullptr)
  requires(std::is_signed_v<I> && std::is_signed_v<J>)
{
  if (value < static_cast<J>(std::numeric_limits<I>::min()) ||
      value > static_cast<J>(std::numeric_limits<I>::max())) {
    throw std::out_of_range(msg ? msg : "out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static constexpr I numeric_cast(J value, const char* msg = nullptr)
  requires(std::is_signed_v<I> && std::is_unsigned_v<J>)
{
  if (value >
      static_cast<std::make_unsigned_t<I>>(std::numeric_limits<I>::max())) {
    throw std::out_of_range(msg ? msg : "out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static constexpr I numeric_cast(J value, const char* msg = nullptr)
  requires(std::is_unsigned_v<I> && std::is_signed_v<J>)
{
  if (value < 0 || static_cast<std::make_unsigned_t<J>>(value) >
                       std::numeric_limits<I>::max()) {
    throw std::out_of_range(msg ? msg : "out of range");
  }
  return static_cast<I>(value);
}

template <typename I, typename J>
static constexpr I numeric_cast(J value, const char* msg = nullptr)
  requires(std::is_unsigned_v<I> && std::is_unsigned_v<J>)
{
  if (value > std::numeric_limits<I>::max()) {
    throw std::out_of_range(msg ? msg : "out of range");
  }
  return static_cast<I>(value);
}

// For bytes we just cast it directly - there are no smaller types anyway
template <typename I>
static constexpr I numeric_cast(const std::byte& value) {
  return static_cast<I>(value);
}

// Convenience functions

/**
 * Convenience for literal byte values.
 */
static constexpr std::byte operator""_b(unsigned long long arg) {
  // std::byte is not an arithmetic type so we can't check min and max of it.
  // Still we want to ensure that the input fits in one byte, so using uint8_t
  // instead for the check.
  static_assert(sizeof(uint8_t) == sizeof(std::byte));
  if (arg > std::numeric_limits<uint8_t>::max()) {
    throw std::out_of_range("Value " + std::to_string(arg) +
                            " not in the range of a byte");
  }
  return static_cast<std::byte>(arg);
}
/**
 * Convenience for literal byte values.
 */
static constexpr std::byte operator""_b(char arg) noexcept {
  return static_cast<std::byte>(arg);
}

/**
 * Extract big endian value to int
 */
template <std::integral T>
static inline T from_big_endian(const bytes& buf, bytes::size_type offset = 0) {
  constexpr auto size = sizeof(T);
  static_assert(size == 2 || size == 4 || size == 8,
                "from_big_endian only supported on 16, 32 or 64 bit types");

  if (offset + size > buf.size() ||
      offset > std::numeric_limits<bytes::size_type>::max() - size) {
    throw std::out_of_range(fmt::format(
        "Target range outside of buffer: ({},{})", offset, buf.size()));
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  if constexpr (size == 2) {
    return static_cast<T>(static_cast<uint8_t>(buf[offset + 1]) << 0 |
                          static_cast<uint8_t>(buf[offset + 0]) << 8);
  } else if constexpr (size == 4) {
    return static_cast<T>(static_cast<uint8_t>(buf[offset + 3])) << 0 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 2])) << 8 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 1])) << 16 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 0])) << 24;
  } else if constexpr (size == 8) {
    return static_cast<T>(static_cast<uint8_t>(buf[offset + 7])) << 0 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 6])) << 8 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 5])) << 16 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 4])) << 24 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 3])) << 32 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 2])) << 40 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 1])) << 48 |
           static_cast<T>(static_cast<uint8_t>(buf[offset + 0])) << 56;
  } else {
    static_assert(!sizeof(T*), "Unhandled size");
  }

  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

/**
 * Convert host int to byte vector in network byte order.
 */
template <std::integral T>
static inline bytes to_big_endian(T val) {
  constexpr auto size = sizeof(T);
  static_assert(size == 2 || size == 4 || size == 8,
                "to_big_endian only supported on 16, 32 or 64 bit types");

  if constexpr (size == 2) {
    return bytes{static_cast<std::byte>((val >> 8) & 0xFF),
                 static_cast<std::byte>((val >> 0) & 0xFF)};
  } else if constexpr (size == 4) {
    return bytes{static_cast<std::byte>((val >> 24) & 0xFF),
                 static_cast<std::byte>((val >> 16) & 0xFF),
                 static_cast<std::byte>((val >> 8) & 0xFF),
                 static_cast<std::byte>((val >> 0) & 0xFF)};
  } else if constexpr (size == 8) {
    return bytes{static_cast<std::byte>((val >> 56) & 0xFF),
                 static_cast<std::byte>((val >> 48) & 0xFF),
                 static_cast<std::byte>((val >> 40) & 0xFF),
                 static_cast<std::byte>((val >> 32) & 0xFF),
                 static_cast<std::byte>((val >> 24) & 0xFF),
                 static_cast<std::byte>((val >> 16) & 0xFF),
                 static_cast<std::byte>((val >> 8) & 0xFF),
                 static_cast<std::byte>((val >> 0) & 0xFF)};
  } else {
    static_assert(!sizeof(T*), "Unhandled size");
  }
}

/**
 * Convert range to a byte vector
 */
template <std::ranges::input_range Range>
bytes to_bytes(const Range& range) {
  zit::bytes ret;
  std::ranges::transform(range, std::back_inserter(ret),
                         [](auto v) { return static_cast<std::byte>(v); });
  return ret;
}

}  // namespace zit
