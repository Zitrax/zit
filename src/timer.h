#pragma once
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>

namespace zit {

class Timer {
 public:
  Timer(const std::string& name)
      : m_name(name), m_start(std::chrono::system_clock::now()) {}

  ~Timer() {
    const std::chrono::duration<double> total =
        std::chrono::system_clock::now() - m_start;
    spdlog::get("console")->info("{} seconds spent on {}", total.count(),
                                 m_name);
  }

 private:
  std::string m_name;
  std::chrono::time_point<std::chrono::system_clock> m_start;
};

}  // namespace zit
