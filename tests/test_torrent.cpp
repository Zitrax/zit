// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "torrent.h"

namespace fs = std::filesystem;

TEST(torrent, construct_single) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(data_dir / "test.torrent");

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
  EXPECT_EQ(t.info_hash().str(),
            "\x8c\x4a\xdb\xf9\xeb\xe6\x6f\x1d\x80\x4f"
            "\xb6\xa4\xfb\x9b\x74\x96\x6c\x3a\xb6\x09");
}

TEST(torrent, construct_single_2) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(data_dir / "test2.torrent");

  EXPECT_EQ(t.announce(), "https://torrent.ubuntu.com/announce");
  const auto announce_list = t.announce_list();
  EXPECT_EQ(t.announce_list().size(), 0);
  EXPECT_EQ(t.creation_date(), 1581513546);
  EXPECT_EQ(t.comment(), "Kubuntu CD cdimage.ubuntu.com");
  EXPECT_EQ(t.created_by(), "");
  EXPECT_EQ(t.encoding(), "");
  EXPECT_EQ(t.piece_length(), 524288);
  EXPECT_FALSE(t.pieces().empty());
  EXPECT_FALSE(t.is_private());
  EXPECT_EQ(t.name(), "kubuntu-18.04.4-desktop-i386.iso");
  EXPECT_EQ(t.length(), 1999503360);
  EXPECT_EQ(t.md5sum(), "");
  EXPECT_TRUE(t.files().empty());
  EXPECT_TRUE(t.is_single_file());
  EXPECT_EQ(t.info_hash().str(),
            "\x49\xC6\x33\x2D\x5A\x3A\x26\x5C\xBD\xBB"
            "\x8F\xC8\xB4\xC0\x97\xC7\xF3\x1A\x8B\x85");
}

TEST(torrent, construct_multi) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(data_dir / "multi_kali.torrent");

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
  EXPECT_EQ(t.length(), 3192651871);
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

  const auto data_dir = fs::path(DATA_DIR);
  EXPECT_THROW(zit::Torrent t(data_dir / "empty.torrent"), std::runtime_error);
  EXPECT_THROW(zit::Torrent t(data_dir / "invalid.torrent"),
               std::invalid_argument);
}
