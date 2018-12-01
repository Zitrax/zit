// -*- mode:c++; c-basic-offset : 2; - * -
#include "gtest/gtest.h"
#include "torrent.h"

TEST(torrent, construct_single) {
  std::filesystem::path p(__FILE__);
  zit::Torrent t(p.parent_path() / "data" / "test.torrent");

  EXPECT_EQ(t.announce(), "http://torrent.ubuntu.com:6969/announce");
  const auto announce_list = t.announce_list();
  EXPECT_EQ(t.announce_list().size(), 2);
  EXPECT_EQ(announce_list[0].size(), 1);
  EXPECT_EQ(announce_list[0][0], "http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(announce_list[1].size(), 1);
  EXPECT_EQ(announce_list[1][0],
            "http://ipv6.torrent.ubuntu.com:6969/announce");
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
  EXPECT_EQ(t.info_hash(),
            "\x8c\x4a\xdb\xf9\xeb\xe6\x6f\x1d\x80\x4f"
            "\xb6\xa4\xfb\x9b\x74\x96\x6c\x3a\xb6\x09");
}

TEST(torrent, construct_multi) {
  std::filesystem::path p(__FILE__);
  zit::Torrent t(p.parent_path() / "data" / "multi.torrent");

  EXPECT_EQ(t.announce(), "http://tracker.kali.org:6969/announce");
  const auto& announce_list = t.announce_list();
  EXPECT_EQ(t.announce_list().size(), 2);
  EXPECT_EQ(announce_list[0].size(), 1);
  EXPECT_EQ(announce_list[0][0], "http://tracker.kali.org:6969/announce");
  EXPECT_EQ(announce_list[1].size(), 1);
  EXPECT_EQ(announce_list[1][0], "udp://tracker.kali.org:6969/announce");
  EXPECT_EQ(t.creation_date(), 1537436744);
  EXPECT_EQ(t.comment(),
            "kali-linux-2018.3a-amd64.iso from https://www.kali.org/");
  EXPECT_EQ(t.created_by(), "mktorrent 1.1");
  EXPECT_EQ(t.encoding(), "");
  EXPECT_EQ(t.piece_length(), 262144);
  EXPECT_FALSE(t.pieces().empty());
  EXPECT_FALSE(t.is_private());
  EXPECT_EQ(t.name(), "kali-linux-2018-3a-amd64-iso");
  EXPECT_EQ(t.length(), 0);
  EXPECT_EQ(t.md5sum(), "");
  EXPECT_FALSE(t.files().empty());
  EXPECT_FALSE(t.is_single_file());

  EXPECT_EQ(t.files().size(), 2);
  const auto file1 = t.files()[0];
  EXPECT_EQ(file1.length(), 3192651776);
  EXPECT_EQ(file1.md5sum(), "");
  EXPECT_EQ(file1.path(), "kali-linux-2018.3a-amd64.iso");
  const auto file2 = t.files()[1];
  EXPECT_EQ(file2.length(), 95);
  EXPECT_EQ(file2.md5sum(), "");
  EXPECT_EQ(file2.path(), "kali-linux-2018.3a-amd64.iso.txt.sha256sum");
}

TEST(torrent, construct_fail) {
  EXPECT_THROW(zit::Torrent t("FOO"), std::runtime_error);

  std::filesystem::path p(__FILE__);
  EXPECT_THROW(zit::Torrent t(p.parent_path() / "data" / "empty.torrent"),
               std::runtime_error);
  EXPECT_THROW(zit::Torrent t(p.parent_path() / "data" / "invalid.torrent"),
               std::invalid_argument);
}
