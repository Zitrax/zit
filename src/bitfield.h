// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "types.h"

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
class bitfield {
 public:
  explicit bitfield(bytes raw) : m_bytes(std::move(raw)) {}
  bitfield() = default;

  /**
   * Proxy class to be able to use operator[] to set values.
   */
  class proxy {
   public:
    proxy(bitfield& bf, bytes::size_type i);

    // Special member functions (cppcoreguidelines-special-member-functions)
    ~proxy() = default;
    proxy(const proxy& other) = delete;
    proxy(const proxy&& other) = delete;
    proxy& operator=(const proxy& rhs) = delete;

    // For lvalue uses

    // Should not need noexcept since the moved object is the proxy
    // not the underlying object.
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    proxy& operator=(proxy&& other);
    proxy& operator=(bool b);

    // For rvalue uses
    operator bool() const;

   private:
    bitfield& m_bitfield;
    bytes::size_type m_i;
  };

  const proxy operator[](bytes::size_type i) const;
  proxy operator[](bytes::size_type i);

 private:
  bytes m_bytes{};
};

}  // namespace zit
