#pragma once
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>

using namespace std::chrono;

namespace zit {

class Timer {
 public:
  Timer(const std::string& name) : m_name(name), m_start(system_clock::now()) {}

  ~Timer() {
    const duration<double> total = system_clock::now() - m_start;
    spdlog::get("console")->info("{} seconds spent on {}", total.count(),
                                 m_name);
  }

 private:
  std::string m_name;
  time_point<system_clock> m_start;
};

}  // namespace zit