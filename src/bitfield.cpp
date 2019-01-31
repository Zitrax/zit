// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.h"

#include <iostream>

namespace zit {

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
  auto byte_index = m_i / 8;
  if (m_bitfield.m_bytes.size() <= byte_index) {
    m_bitfield.m_bytes.resize(byte_index + 1);
  }
  auto byte_val = static_cast<uint8_t>(m_bitfield.m_bytes[byte_index]);
  // Update bit
  if (b) {
    byte_val |= static_cast<uint8_t>(1 << (m_i % 8));
  } else {
    byte_val &= static_cast<uint8_t>(~(1 << (m_i % 8)));
  }
  m_bitfield.m_bytes[byte_index] = static_cast<std::byte>(byte_val);
  return *this;
}

Bitfield::Proxy::operator bool() const {
  auto byte = static_cast<uint8_t>(m_bitfield.m_bytes.at(m_i / 8));
  return byte & (1 << (m_i % 8));
}

// --- bitfield ---

Bitfield::Proxy Bitfield::operator[](bytes::size_type i) const {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  return Bitfield::Proxy(const_cast<Bitfield&>(*this), i);
}

Bitfield::Proxy Bitfield::operator[](bytes::size_type i) {
  return Bitfield::Proxy(*this, i);
}

std::optional<bytes::size_type> Bitfield::next(bool val) {
  // First find relevant byte
  auto it = std::find_if(m_bytes.begin(), m_bytes.end(), [&val](const auto b) {
    return val ? static_cast<uint8_t>(b) > 0 : static_cast<uint8_t>(b) < 255;
  });
  if (it != m_bytes.end()) {
    auto offset =
        numeric_cast<bytes::size_type>(std::distance(m_bytes.begin(), it) * 8);
    for (unsigned short i = 0; i < 8; ++i) {
      if (operator[](offset + i) == val) {
        return std::make_optional<bytes::size_type>(offset + i);
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
