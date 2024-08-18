// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <memory>

/**
 * Simple implementation of a StrongType, used like so:
 *
 * @code {.cpp}
 * using Port = StrongType<unsigned, struct PortTag>;
 * Port p{12};
 * std::cout << p.get() << "\n";
 * @endcode
 *
 */
template <typename T, typename Tag>
class StrongType {
 public:
  explicit StrongType(T t) : m_val(std::move(t)) {}

  [[nodiscard]] constexpr T& get() noexcept { return m_val; }
  [[nodiscard]] constexpr const T& get() const noexcept { return m_val; }

  // Comparison support for comparable types using spaceship
  auto operator<=>(const StrongType& other) const = default;

 private:
  T m_val;
};
