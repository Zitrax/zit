// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <array>

#include "types.h"

namespace zit {

constexpr auto SHA_LENGTH = 20;

/**
 * Representation of a sha1 calculation.
 *
 * Use calculate() on data to generate a sha1.
 */
class sha1 : public std::array<char, SHA_LENGTH> {
 public:
  sha1();
  explicit sha1(const std::string& val);

  std::string str() const;

  static sha1 calculate(const std::string& data);

  /**
   * Extract a raw sha1 from a byte vector (no calculation involved).
   */
  static sha1 from_bytes(const bytes& buffer, bytes::size_type offset);
};

}  // namespace zit
