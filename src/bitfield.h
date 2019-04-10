// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "types.h"

#include <optional>

// Needed for spdlog to handle operator<<
#include "spdlog/fmt/ostr.h"

namespace zit {

/**
 * Data structure representing bools in an array of bytes where each bool is a
 * bit.
 *
 * Note that this implementation is not optimal in terms of space usage, it will
 * at least store bytes up to the largest bit needed. I.e. if you set only bit
 * 1024 we will use 1024/8=128 bytes since the 1024th bit is in the 128th byte.
 * It's specifically targeted at the torrent bitfields which follows this
 * scheme.
 *
 * std::vector<bool> does not seem to have a way to access the raw bytes, and
 * don't want any external lib so writing my own.
 */
class Bitfield {
 public:
  explicit Bitfield(bytes raw) : m_bytes(std::move(raw)) {}

  /**
   * @param count Number of default initialized (0) bits.
   * @note The result will be a bitfield of the number of bytes fitting
   *       the number of bits needed.
   */
  explicit Bitfield(bytes::size_type count);

  Bitfield() = default;

  /**
   * Proxy class to be able to use operator[] to set values.
   */
  class Proxy {
   public:
    Proxy(Bitfield& bf, bytes::size_type i);

    // Special member functions (cppcoreguidelines-special-member-functions)
    ~Proxy() = default;
    Proxy(const Proxy& other) = delete;
    Proxy(const Proxy&& other) = delete;
    Proxy& operator=(const Proxy& rhs) = delete;

    // For lvalue uses

    // Should not need noexcept since the moved object is the proxy
    // not the underlying object.
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    Proxy& operator=(Proxy&& rhs);
    Proxy& operator=(bool b);

    // For rvalue uses
    // NOLINTNEXTLINE(hicpp-explicit-conversions)
    operator bool() const;

   private:
    uint8_t bit() const;

    Bitfield& m_bitfield;
    bytes::size_type m_i;
  };

  Proxy operator[](bytes::size_type i) const;
  Proxy operator[](bytes::size_type i);

  /**
   * Subtract another bitfield.
   */
  Bitfield operator-(const Bitfield& other) const;

  /**
   * Number of bits contained.
   */
  [[nodiscard]] auto size() const { return m_bytes.size() * 8; }

  /**
   * Number of bytes contained.
   */
  [[nodiscard]] auto size_bytes() const { return m_bytes.size(); }

  /**
   * Return next index with value = val
   */
  [[nodiscard]] std::optional<bytes::size_type> next(bool val) const;

 private:
  bytes m_bytes{};
};

std::ostream& operator<<(std::ostream& os, const Bitfield& bf);

}  // namespace zit
