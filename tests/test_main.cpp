// -*- mode:c++; c-basic-offset : 2; -*-
#include "test_main.hpp"
#include <gtest/gtest.h>
#include <csignal>
#include "logger.hpp"

decltype(sigint_function) sigint_function = [](int /*s*/) {
  zit::logger()->warn("CTRL-C not implemented for test! Hard exiting...");
  std::exit(1);
};
namespace {
void sigint_handler(int s) {
  sigint_function(s);
}
}  // namespace

int main(int argc, char** argv) {
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
