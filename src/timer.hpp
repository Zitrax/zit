#pragma once
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>

namespace zit {

class Timer {
 public:
  explicit Timer(std::string name) : m_name(std::move(name)) {}

  ~Timer() {
    const std::chrono::duration<double> total =
        std::chrono::system_clock::now() - m_start;
    logger()->info("{:.3f} seconds spent on {}", total.count(), m_name);
  }

 private:
  std::string m_name;
  std::chrono::time_point<std::chrono::system_clock> m_start{
      std::chrono::system_clock::now()};
};

}  // namespace zit
