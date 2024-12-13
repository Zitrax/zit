// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "types.hpp"

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
    // NOLINTNEXTLINE(performance-noexcept-move-constructor,cppcoreguidelines-noexcept-move-operations)
    Proxy& operator=(Proxy&& rhs);
    Proxy& operator=(bool b);

    // For rvalue uses
    // NOLINTNEXTLINE(hicpp-explicit-conversions)
    operator bool() const;

   private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Bitfield& m_bitfield;
    bytes::size_type m_i;
  };

  /**
   * Get bit value.
   */
  [[nodiscard]] bool get(bytes::size_type i) const;

  /**
   * Get bit value.
   */
  bool operator[](bytes::size_type i) const;

  /**
   * Get bit value.
   */
  Proxy operator[](bytes::size_type i);

  /**
   * Fill the first n bits with 0 or 1.
   *
   * @param count number of bits to set
   * @param val value to set
   * @param start start index
   */
  void fill(bytes::size_type count, bool val, bytes::size_type start = 0);

  /**
   * Subtract another bitfield.
   */
  Bitfield operator-(const Bitfield& other) const;

  /**
   * Add/or another bitfield.
   */
  Bitfield operator+(const Bitfield& other) const;

  /**
   * Return the number of true bits in the set.
   */
  [[nodiscard]] std::size_t count() const;

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
  [[nodiscard]] std::optional<bytes::size_type> next(
      bool val,
      bytes::size_type start = 0) const;

  /**
   * The raw byte vector.
   */
  [[nodiscard]] const bytes& data() const { return m_bytes; }

 private:
  bytes m_bytes{};
};

std::ostream& operator<<(std::ostream& os, const Bitfield& bf);

/** For fmt */
std::string format_as(const Bitfield& bf);

}  // namespace zit
