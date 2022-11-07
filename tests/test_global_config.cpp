// -*- mode:c++; c-basic-offset : 2; -*-
#include <stdexcept>
#include "file_utils.hpp"
#include "global_config.hpp"
#include "gtest/gtest.h"
#include "test_utils.hpp"

using namespace zit;

TEST(FileConfigSingleton, SingletonDirectoryFileConfigConstruct) {
  // Basic verification that we do not throw
  [[maybe_unused]] const auto& config =
      SingletonDirectoryFileConfig::getInstance();
}

using FileConfigTest = TestWithTmpDir;

TEST_F(FileConfigTest, EmptyFile) {
  const auto config_file = tmp_dir() / ".zit";
  write_file(config_file, "");
  const auto config = FileConfig{config_file};
  EXPECT_TRUE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
}

TEST_F(FileConfigTest, InvalidFile) {
  const auto config_file = tmp_dir() / ".zit";
  write_file(config_file, "foo=bar");
  const auto config = FileConfig{config_file};
  EXPECT_TRUE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
}

TEST_F(FileConfigTest, CorrectFile) {
  const auto config_file = tmp_dir() / ".zit";
  write_file(config_file, "initiate_peer_connections=false");
  const auto config = FileConfig{config_file};
  EXPECT_FALSE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
}
