// -*- mode:c++; c-basic-offset : 2; - * -
#include "gtest/gtest.h"
#include "torrent.h"

TEST(torrent, construct) {
  std::filesystem::path p(__FILE__);
  zit::Torrent t(p.parent_path() /= "test.torrent");

  EXPECT_EQ(t.announce(), "http://torrent.ubuntu.com:6969/announce");
  const auto announce_list = t.announce_list();
  EXPECT_EQ(t.announce_list().size(), 2);
  EXPECT_EQ(announce_list[0].size(), 1);
  EXPECT_EQ(announce_list[0][0], "http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(announce_list[1].size(), 1);
  EXPECT_EQ(announce_list[1][0], "http://ipv6.torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(t.creation_date(), 1539860630);
  EXPECT_EQ(t.comment(), "Ubuntu CD releases.ubuntu.com");
  EXPECT_EQ(t.created_by(), "");
  EXPECT_EQ(t.encoding(), "");
  EXPECT_EQ(t.piece_length(), 524288);
  EXPECT_FALSE(t.pieces().empty());
  EXPECT_FALSE(t.is_private());
  EXPECT_EQ(t.name(), "ubuntu-18.10-live-server-amd64.iso");
  EXPECT_EQ(t.length(), 923795456);
  EXPECT_EQ(t.md5sum(), "");
  EXPECT_TRUE(t.files().empty());
  EXPECT_TRUE(t.is_single_file());
}

TEST(torrent, construct_fail) {
    EXPECT_THROW(zit::Torrent t("FOO"), std::ios_base::failure);

    std::filesystem::path p(__FILE__);
    EXPECT_THROW(zit::Torrent t(p.parent_path() /= "empty.torrent"), std::ios_base::failure);
    EXPECT_THROW(zit::Torrent t(p.parent_path() /= "invalid.torrent"), std::invalid_argument);
}
