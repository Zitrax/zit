#pragma once

#include "logger.hpp"

namespace zit {

/**
 * ScopeGuard is a class that will call a function when it goes out of scope.
 */
class ScopeGuard {
 public:
  template <typename Callable>
  explicit ScopeGuard(Callable&& onExit)
      : m_exit_fn(std::forward<Callable>(onExit)) {}

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& other) = delete;

  ~ScopeGuard() {
    try {
      m_exit_fn();
    } catch (...) {
      try {
        logger()->warn("Exception thrown from ScopeGuard");
      } catch (...) {
      }
    }
  }

 private:
  std::function<void()> m_exit_fn;
};

}  // namespace zit
