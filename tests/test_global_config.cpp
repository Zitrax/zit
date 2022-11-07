// -*- mode:c++; c-basic-offset : 2; -*-
#include "global_config.hpp"
#include "gtest/gtest.h"

using namespace zit;

// TODO: Add tests that actually read a test config file

TEST(global_config, FileConstruct) {
  // Basic verification that we do not throw
  [[maybe_unused]] const auto& config = FileConfig::getInstance();
}
