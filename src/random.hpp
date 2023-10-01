#pragma once

#include <random>

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

}  // namespace zit
