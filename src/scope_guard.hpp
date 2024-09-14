#pragma once

namespace zit {

class ScopeGuard {
 public:
  template <typename Callable>
  ScopeGuard(Callable&& onExit) : m_exit_fn(std::forward<Callable>(onExit)) {}

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& other) = delete;

  ~ScopeGuard() { m_exit_fn(); }

 private:
  std::function<void()> m_exit_fn;
};

}  // namespace zit
