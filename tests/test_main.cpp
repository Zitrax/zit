// -*- mode:c++; c-basic-offset : 2; -*-
#include "test_main.hpp"
#include <gtest/gtest.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <csignal>

decltype(sigint_function) sigint_function = [](int /*s*/) {
  spdlog::get("console")->warn(
      "CTRL-C not implemented for test! Hard exiting...");
  std::exit(1);
};
namespace {
void sigint_handler(int s) {
  sigint_function(s);
}
}  // namespace

int main(int argc, char** argv) {
  // Needed since the code assumes a global log object
  auto console = spdlog::stdout_color_mt("console");
  spdlog::cfg::load_env_levels();

#ifndef WIN32
  struct sigaction sigIntHandler {};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  sigIntHandler.sa_handler = sigint_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, nullptr);
#endif  // !WIN32

  // Run the tests
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
