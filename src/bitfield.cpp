// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.h"

#include <iostream>

namespace zit {

// --- bitfield::proxy ---

bitfield::proxy::proxy(bitfield& bf, bytes::size_type i)
    : m_bitfield(bf), m_i(i) {}

// NOLINTNEXTLINE(performance-noexcept-move-constructor)
bitfield::proxy& bitfield::proxy::operator=(bitfield::proxy&& rhs) {
  operator=(static_cast<bool>(rhs));
  return *this;
}

bitfield::proxy& bitfield::proxy::operator=(bool b) {
  // Get relevant byte
  auto byte_index = m_i / 8;
  if (m_bitfield.m_bytes.size() <= byte_index) {
    m_bitfield.m_bytes.resize(byte_index + 1);
  }
  uint8_t byte_val = static_cast<uint8_t>(m_bitfield.m_bytes[byte_index]);
  // Update bit
  if (b) {
    byte_val |= static_cast<uint8_t>(1 << (m_i % 8));
  } else {
    byte_val &= static_cast<uint8_t>(~(1 << (m_i % 8)));
  }
  m_bitfield.m_bytes[byte_index] = static_cast<std::byte>(byte_val);
  return *this;
}

bitfield::proxy::operator bool() const {
  auto byte = static_cast<uint8_t>(m_bitfield.m_bytes.at(m_i / 8));
  return byte & (1 << (m_i % 8));
}

// --- bitfield ---

const bitfield::proxy bitfield::operator[](bytes::size_type i) const {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  return bitfield::proxy(const_cast<bitfield&>(*this), i);
}

bitfield::proxy bitfield::operator[](bytes::size_type i) {
  return bitfield::proxy(*this, i);
}

std::ostream& operator<<(std::ostream& os, const bitfield& bf) {
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
