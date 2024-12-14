// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.hpp"
#include "types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>

namespace zit {

namespace {
uint8_t bit_mask(bytes::size_type i) {
  return static_cast<uint8_t>(1 << (7 - (i % 8)));
}
}  // namespace

// --- bitfield::proxy ---

Bitfield::Proxy::Proxy(Bitfield& bf, bytes::size_type i)
    : m_bitfield(bf), m_i(i) {}

// NOLINTNEXTLINE(performance-noexcept-move-constructor,cppcoreguidelines-noexcept-move-operations)
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

bool Bitfield::get(bytes::size_type i) const {
  const auto byte = static_cast<uint8_t>(m_bytes.at(i / 8));
  return byte & bit_mask(i);
}

bool Bitfield::operator[](bytes::size_type i) const {
  return get(i);
}

Bitfield::Proxy Bitfield::operator[](bytes::size_type i) {
  return {*this, i};
}

void Bitfield::fill(bytes::size_type count, bool val, bytes::size_type start) {
  std::cerr << fmt::format("Fill: {} {} {} {}", count, val, start,
                           m_bytes.size())
            << "\n";
  if (count == 0) {
    return;
  }

  const auto bit_end = start + count;
  auto byte_start = start / 8;
  auto byte_end = (bit_end - 1) / 8;

  if (byte_end >= m_bytes.size()) {
    throw std::invalid_argument("Bitfield::fill: Out of range");
  }

  if (byte_start == byte_end) {
    for (auto i = start; i < bit_end; ++i) {
      operator[](i) = val;
    }
    return;
  }

  // Fill the first byte with the remaining bits
  if (start % 8) {
    // Mask for the first byte, for example if we start at 3 we want to set
    // the first 5 bits to 1, i.e. 0b00011111
    const auto mask = static_cast<uint8_t>(0xFF >> start % 8);
    m_bytes[byte_start] =
        val ? std::byte{static_cast<uint8_t>(
                  static_cast<uint8_t>(m_bytes[byte_start]) | mask)}
            : std::byte{static_cast<uint8_t>(
                  static_cast<uint8_t>(m_bytes[byte_start]) & ~mask)};
    byte_start++;
  }
  // Fill the last byte with the remaining bits
  if (bit_end % 8) {
    // Mask for the end byte, for example if we end at 10 we want to set
    // the last 2 bits to 1, i.e. 0b11000000
    const auto mask = static_cast<uint8_t>(0xFF << (8 - (bit_end % 8)));
    m_bytes[byte_end] =
        val ? std::byte{static_cast<uint8_t>(
                  static_cast<uint8_t>(m_bytes[byte_end]) | mask)}
            : std::byte{static_cast<uint8_t>(
                  static_cast<uint8_t>(m_bytes[byte_end]) & ~mask)};
    byte_end--;
  }

  // Fill the rest of the bytes
  const auto byte_val = val ? std::byte{0xFF} : std::byte{0x00};
  using difftype = decltype(m_bytes)::iterator::difference_type;
  if (byte_start <= byte_end) {
    std::fill(m_bytes.begin() + numeric_cast<difftype>(byte_start),
              m_bytes.begin() + numeric_cast<difftype>(byte_end + 1), byte_val);
  }
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
      if (get(offset + i) == val) {
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

std::string format_as(const Bitfield& bf) {
  std::stringstream ss;
  ss << bf;
  return ss.str();
}

}  // namespace zit
