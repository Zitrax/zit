#pragma once

#include <algorithm>
#include <random>
#include <string>

#include "types.hpp"

namespace zit {

/**
 * Generate a random number of type T
 */
template <typename T>
inline T random_value() {
  static auto engine = std::mt19937(std::random_device{}());
  auto distribution = std::uniform_int_distribution<T>{
      std::numeric_limits<T>::min(), std::numeric_limits<T>::max()};
  return distribution(engine);
}

// Based on answers in https://stackoverflow.com/q/440133/11722
inline auto random_string(std::size_t len) -> std::string {
  static std::string chars =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  thread_local static std::mt19937 rg{std::random_device{}()};
  thread_local static auto dist = std::uniform_int_distribution<unsigned long>{
      {}, zit::numeric_cast<unsigned long>(chars.size()) - 1};
  std::string result(len, '\0');
  std::generate_n(begin(result), len, [&] { return chars.at(dist(rg)); });
  return result;
}

}  // namespace zit
