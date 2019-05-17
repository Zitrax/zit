// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "spdlog/sinks/stdout_color_sinks.h"

int main(int argc, char** argv) {
  // Needed since the code assumes a global log object
  auto console = spdlog::stdout_color_mt("console");
  console->set_level(spdlog::level::info);

  // Run the tests
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
