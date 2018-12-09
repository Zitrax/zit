// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <array>

namespace zit {

/**
 * Representation of a sha1 calculation.
 *
 * Use calculate() on data to generate a sha1.
 */
class sha1 : public std::array<char, 20> {
 public:
  sha1() = default;
  sha1(const std::string& val);

  static sha1 calculate(const std::string& data);
};

}  // namespace zit
