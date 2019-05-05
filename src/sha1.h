// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <array>
#include <filesystem>

#include "types.h"

namespace zit {

constexpr auto SHA_LENGTH = 20;

/**
 * Representation of a sha1 calculation.
 *
 * Use calculate() on data to generate a sha1.
 */
class Sha1 : public std::array<char, SHA_LENGTH> {
 public:
  Sha1();
  explicit Sha1(const std::string& val);

  [[nodiscard]] std::string str() const;
  [[nodiscard]] std::string hex() const;

  [[nodiscard]] static Sha1 calculate(const std::string& data);
  [[nodiscard]] static Sha1 calculate(const bytes& data);
  [[nodiscard]] static Sha1 calculate(const std::filesystem::path& file);

  /**
   * Extract a raw sha1 from a byte vector (no calculation involved).
   *
   * Implemented for zit::bytes and std::strings.
   */
  template <typename T>
  static Sha1 fromBuffer(const T& buffer, typename T::size_type offset);

 private:
  [[nodiscard]] static Sha1 calculate(const unsigned char* src, size_t count);
};

}  // namespace zit
