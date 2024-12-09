// -*- mode:c++; c-basic-offset : 2; -*-
#include <stdexcept>
#include "bencode.hpp"
#include "file_utils.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "logger.hpp"
#include "spdlog/fmt/ostr.h"
#include "test_utils.hpp"
#include "torrent.hpp"

namespace fs = std::filesystem;

using namespace std::string_literals;
using namespace bencode;

struct torrent : public TestWithIOContext, public ::testing::Test {};

TEST_F(torrent, construct_single) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(m_io_context, data_dir / "test.torrent");

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

TEST_F(torrent, construct_single_2) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(m_io_context, data_dir / "test2.torrent");

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

TEST_F(torrent, construct_multi) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t(m_io_context, data_dir / "multi_kali.torrent");

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

TEST_F(torrent, construct_fail) {
  EXPECT_THROW(zit::Torrent t(m_io_context, "FOO"), std::runtime_error);

  const auto data_dir = fs::path(DATA_DIR);
  EXPECT_THROW(zit::Torrent t(m_io_context, data_dir / "empty.torrent"),
               std::invalid_argument);
  EXPECT_THROW(zit::Torrent t(m_io_context, data_dir / "invalid.torrent"),
               std::invalid_argument);
}

TEST_F(torrent, construct_map) {
  const auto data_dir = fs::path(DATA_DIR);
  EXPECT_EQ(zit::Torrent::count(), 0);
  {
    zit::Torrent t(m_io_context, data_dir / "test.torrent");
    EXPECT_EQ(zit::Torrent::count(), 1);
    EXPECT_NE(zit::Torrent::get(t.info_hash()), nullptr);
  }
  EXPECT_EQ(zit::Torrent::count(), 0);
}

TEST_F(torrent, peer_id) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent torrent(m_io_context, data_dir / "test2.torrent");

  const auto peer_id = torrent.peer_id();

  zit::logger()->debug("peer_id = {}", peer_id);

  EXPECT_EQ(peer_id.size(), 20);
}

// Test that two torrents can listen at once using the same port
TEST_F(torrent, multi_listen) {
  const auto data_dir = fs::path(DATA_DIR);
  zit::Torrent t1(m_io_context, data_dir / "multi_kali.torrent");
  zit::Torrent t2(m_io_context, data_dir / "test.torrent");
}

#ifdef __linux__

// 127.0.0.1:65535
const auto FAKE_URL = "\x7F\x00\x00\x01\xFF\xFF"s;

class torrent_with_tmp_dir : public TestWithTmpDir, public TestWithIOContext {};

TEST_F(torrent_with_tmp_dir, tracker_requests_announce) {
  // Create a torrent file with a known announce config
  BeDict root;
  BeDict info;
  info["piece length"] = Element::build(1);
  info["pieces"] = Element::build("AAAAAAAAAAAAAAAAAAAA");
  info["name"] = Element::build("test");
  info["length"] = Element::build(100);

  root["info"] = Element::build(info);
  root["announce"] = Element::build("http://tracker-url");

  const auto torrent_file = tmp_dir() / "test.torrent";
  zit::write_file(torrent_file, encode(root));

  std::vector<std::string> requests;

  zit::Torrent t(m_io_context, torrent_file, tmp_dir(),
                 zit::SingletonDirectoryFileConfig::getInstance(),
                 [&](const zit::Url& url, const std::string&) {
                   requests.push_back(url.host());
                   BeDict peers;
                   peers["peers"] =
                       Element::build(FAKE_URL);  // Fake binary url (6 bytes)
                   return std::make_pair("", encode(peers));
                 });

  t.start();

  EXPECT_THAT(requests, testing::UnorderedElementsAreArray({"tracker-url"}));
}

TEST_F(torrent_with_tmp_dir, tracker_requests_announce_list) {
  // Create a torrent file with a known announce config
  BeDict root;
  BeDict info;
  info["piece length"] = Element::build(1);
  info["pieces"] = Element::build("AAAAAAAAAAAAAAAAAAAA");
  info["name"] = Element::build("test");
  info["length"] = Element::build(100);

  root["info"] = Element::build(info);
  root["announce"] = Element::build("http://tracker-url");

  BeList tier_a;
  tier_a.emplace_back(Element::build("http://t1_1"));
  tier_a.emplace_back(Element::build("http://t1_2"));
  BeList tier_b;
  tier_b.emplace_back(Element::build("http://t2_1"));
  BeList announce_list;
  announce_list.push_back(Element::build(tier_a));
  announce_list.push_back(Element::build(tier_b));

  root["announce-list"] = Element::build(announce_list);

  const auto torrent_file = tmp_dir() / "test.torrent";
  zit::write_file(torrent_file, encode(root));

  std::vector<std::string> requests;

  zit::Torrent t(
      m_io_context, torrent_file, tmp_dir(),
      zit::SingletonDirectoryFileConfig::getInstance(),
      // Keep track of the requests and make only the last one pass
      [&, call = 0](const zit::Url& url, const std::string&) mutable {
        requests.push_back(url.host());
        BeDict peers;
        peers["peers"] = Element::build(
            call == 2 ? FAKE_URL : "");  // Fake binary url (6 bytes)
        call++;
        return std::make_pair("", encode(peers));
      });

  t.start();

  EXPECT_THAT(requests,
              testing::UnorderedElementsAreArray({"t1_1", "t1_2", "t2_1"}));
}

#endif  // __linux__
