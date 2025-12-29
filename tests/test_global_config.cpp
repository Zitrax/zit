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
  EXPECT_FALSE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
  EXPECT_TRUE(config.get(BoolSetting::RESOLVE_URLS));
  EXPECT_TRUE(config.get(BoolSetting::PIECE_VERIFY_THREADS));
  EXPECT_EQ(config.get(IntSetting::LISTENING_PORT), 20001);
  EXPECT_EQ(config.get(IntSetting::CONNECTION_PORT), 20000);
  EXPECT_EQ(config.get(IntSetting::RETRY_PIECES_INTERVAL_SECONDS), 45);
  EXPECT_EQ(config.get(IntSetting::RETRY_PEERS_INTERVAL_SECONDS), 25);
}

TEST_F(FileConfigTest, InvalidFile) {
  const auto config_file = tmp_dir() / ".zit";
  write_file(config_file, "foo=bar\nlistening_port=nan");
  const auto config = FileConfig{config_file};
  EXPECT_FALSE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
}

TEST_F(FileConfigTest, CorrectFile) {
  const auto config_file = tmp_dir() / ".zit";
  write_file(
      config_file,
      "initiate_peer_connections=true\nlistening_port=123\nconnection_"
      "port=321\nresolve_urls=0\npiece_verify_threads=false\nbind_"
      "address=192.168.5.5\nretry_pieces_interval_seconds=10\nretry_peers_"
      "interval_seconds=20\n");
  const auto config = FileConfig{config_file};
  EXPECT_TRUE(config.get(BoolSetting::INITIATE_PEER_CONNECTIONS));
  EXPECT_FALSE(config.get(BoolSetting::RESOLVE_URLS));
  EXPECT_FALSE(config.get(BoolSetting::PIECE_VERIFY_THREADS));
  EXPECT_EQ(config.get(IntSetting::LISTENING_PORT), 123);
  EXPECT_EQ(config.get(IntSetting::CONNECTION_PORT), 321);
  EXPECT_EQ(config.get(StringSetting::BIND_ADDRESS), "192.168.5.5");
  EXPECT_EQ(config.get(IntSetting::RETRY_PIECES_INTERVAL_SECONDS), 10);
  EXPECT_EQ(config.get(IntSetting::RETRY_PEERS_INTERVAL_SECONDS), 20);
}
