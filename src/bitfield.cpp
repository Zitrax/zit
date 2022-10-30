// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.hpp"
#include "types.hpp"

#include <array>
#include <iostream>
#include <numeric>

namespace zit {

namespace {
  uint8_t bit_mask(bytes::size_type i) {
    return static_cast<uint8_t>(1 << (7 - (i % 8)));
  }
}

// --- bitfield::proxy ---

Bitfield::Proxy::Proxy(Bitfield& bf, bytes::size_type i)
    : m_bitfield(bf), m_i(i) {}

// NOLINTNEXTLINE(performance-noexcept-move-constructor)
Bitfield::Proxy& Bitfield::Proxy::operator=(Bitfield::Proxy&& rhs) {
  operator=(static_cast<bool>(rhs));
  return *this;
}

Bitfield::Proxy& Bitfield::Proxy::operator=(bool b) {
  // Get relevant byte
  const auto byte_index = m_i / 8;
  if (m_bitfield.m_bytes.size() <= byte_index) {
    m_bitfield.m_bytes.resize(byte_index + 1);
  }
  auto byte_val = static_cast<uint8_t>(m_bitfield.m_bytes[byte_index]);
  // Update bit
  const uint8_t cb = bit_mask(m_i);
  if (b) {
    byte_val |= cb;
  } else {
    byte_val &= ~cb;
  }
  m_bitfield.m_bytes[byte_index] = static_cast<std::byte>(byte_val);
  return *this;
}

Bitfield::Proxy::operator bool() const {
  return m_bitfield.get(m_i);
}

// --- bitfield ---

Bitfield::Bitfield(bytes::size_type count) {
  // Number of bytes that will fit given number of bits
  // i.e. ceil(count/8)
  m_bytes.resize(count / 8 + (count % 8 != 0));
}

bool Bitfield::get(bytes::size_type i) const{
  const auto byte = static_cast<uint8_t>(m_bytes.at(i / 8));
  return byte & bit_mask(i);
}

bool Bitfield::operator[](bytes::size_type i) const {
  return get(i);
}

Bitfield::Proxy Bitfield::operator[](bytes::size_type i) {
  return {*this, i};
}

Bitfield Bitfield::operator-(const Bitfield& other) const {
  auto len = std::min(size_bytes(), other.size_bytes());
  Bitfield ret;
  ret.m_bytes.reserve(len);

  for (decltype(len) i = 0; i < len; ++i) {
    ret.m_bytes.emplace_back(m_bytes[i] ^ (m_bytes[i] & other.m_bytes[i]));
  }

  return ret;
}

Bitfield Bitfield::operator+(const Bitfield& other) const {
  auto len = std::min(size_bytes(), other.size_bytes());
  Bitfield ret;
  ret.m_bytes.reserve(len);

  for (decltype(len) i = 0; i < len; ++i) {
    ret.m_bytes.emplace_back(m_bytes[i] | other.m_bytes[i]);
  }

  return ret;
}

static constexpr auto bitsPerByteTable = [] {
  std::array<uint8_t, 256> table{};
  for (decltype(table)::size_type i = 0; i < table.size(); i++) {
    table.at(i) = zit::numeric_cast<uint8_t>(table.at(i / 2) + (i & 1));
  }
  return table;
}();

std::size_t Bitfield::count() const {
  return std::accumulate(
      m_bytes.begin(), m_bytes.end(), static_cast<std::size_t>(0),
      [](std::size_t c, const std::byte& b) {
        return c + bitsPerByteTable.at(static_cast<uint8_t>(b));
      });
}

std::optional<bytes::size_type> Bitfield::next(bool val,
                                               bytes::size_type start) const {
  if (start > size()) {
    return {};
  }

  auto byte_offset = numeric_cast<long>(start / 8);
  const auto bit_offset = numeric_cast<unsigned short>(start % 8);

  // Check first partial byte if we have a bit offset
  if (bit_offset) {
    for (unsigned short i = bit_offset; i < 8; ++i) {
      const auto pos = numeric_cast<bytes::size_type>(byte_offset * 8 + i);
      if (operator[](pos) == val) {
        return pos;
      }
    }
    byte_offset++;
  }

  // Find relevant byte
  auto it = std::find_if(m_bytes.begin() + byte_offset, m_bytes.end(),
                         [&val](const auto B) {
                           return val ? static_cast<uint8_t>(B) > 0
                                      : static_cast<uint8_t>(B) < 255;
                         });

  // Find matching bit in byte
  if (it != m_bytes.end()) {
    auto offset =
        numeric_cast<bytes::size_type>(std::distance(m_bytes.begin(), it) * 8);
    for (unsigned short i = 0; i < 8; ++i) {
      if (operator[](offset + i) == val) {
        return offset + i;
      }
    }
  }

  return {};
}

std::ostream& operator<<(std::ostream& os, const Bitfield& bf) {
  os << "-bitfield-\n";
  auto size = bf.size();
  for (unsigned long i = 0; i < size; ++i) {
    os << bf[i];
    if (i != size - 1) {
      if (i % 1000 == 999) {
        os << "\n\n";
      } else if (i % 100 == 99) {
        os << "\n";
      } else if (i % 10 == 9) {
        os << " ";
      }
    }
  }
  os << "\n----------\n";
  return os;
}

}  // namespace zit
