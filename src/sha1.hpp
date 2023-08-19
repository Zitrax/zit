// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <array>
#include <filesystem>

#include "types.hpp"

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

  /**
   * Calculate SHA1 of the string content.
   */
  [[nodiscard]] static Sha1 calculateData(const std::string& data);

  /**
   * Calculate SHA1 of the byte vector content.
   */
  [[nodiscard]] static Sha1 calculateData(const bytes& data);

  /**
   * Calculate SHA1 of the content of the file.
   */
  [[nodiscard]] static Sha1 calculateFile(const std::filesystem::path& file);

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

std::ostream& operator<<(std::ostream& os, const Sha1& sha1);

/** For fmt */
std::string format_as(const Sha1& sha1);

}  // namespace zit
