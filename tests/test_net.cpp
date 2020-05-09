// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "net.h"

#include <asio.hpp>
using asio::detail::socket_ops::host_to_network_short;

using namespace zit;
using namespace std;

TEST(net, url_encode) {
  string input =
      "\x12\x34\x56\x78\x9a\xbc\xde\xf1\x23\x45"
      "\x67\x89\xab\xcd\xef\x12\x34\x56\x78\x9a";
  EXPECT_EQ(Net::urlEncode(input),
            "%124Vx%9A%BC%DE%F1%23Eg%89%AB%CD%EF%124Vx%9A");
}

TEST(net, url_binary) {
  uint16_t port = host_to_network_short(10000);
  // 192.168.0.1:1000
  std::string urlstr = {static_cast<char>(192),
                        static_cast<char>(168),
                        0,
                        1,
                        static_cast<char>(port & 0xFF),
                        static_cast<char>((port & 0xFF00) >> 8)};
  Url url(urlstr, true);
  EXPECT_EQ(url.scheme(), "http");
  EXPECT_EQ(url.host(), "192.168.0.1");
  EXPECT_EQ(url.port(), 10000);
  EXPECT_EQ(url.path(), "");
}

TEST(net, url_string) {
  Url url1("https://torrent.ubuntu.com/announce");
  EXPECT_EQ(url1.scheme(), "https");
  EXPECT_EQ(url1.host(), "torrent.ubuntu.com");
  EXPECT_EQ(url1.port(), std::nullopt);
  EXPECT_EQ(url1.service(), "https");
  EXPECT_EQ(url1.path(), "/announce");

  Url url2("http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(url2.scheme(), "http");
  EXPECT_EQ(url2.host(), "torrent.ubuntu.com");
  EXPECT_EQ(url2.port(), 6969);
  EXPECT_EQ(url2.service(), "6969");
  EXPECT_EQ(url2.path(), "/announce");

  Url url3(
      "https://torrent.ubuntu.com/"
      "announce?info_hash=I%C63-Z%3A%26%5C%BD%BB%8F%C8%B4%C0%97%C7%F3%1A%8B%85&"
      "peer_id=abcdefghijklmnopqrst&port=20001&uploaded=0&downloaded=0&left="
      "1999503360&event=started&compact=1");
  EXPECT_EQ(url3.scheme(), "https");
  EXPECT_EQ(url3.host(), "torrent.ubuntu.com");
  EXPECT_EQ(url3.port(), std::nullopt);
  EXPECT_EQ(url3.service(), "https");
  EXPECT_EQ(
      url3.path(),
      "/announce?info_hash=I%C63-Z%3A%26%5C%BD%BB%8F%C8%B4%C0%97%C7%F3%1A%8B%"
      "85&"
      "peer_id=abcdefghijklmnopqrst&port=20001&uploaded=0&downloaded=0&left="
      "1999503360&event=started&compact=1");
}

TEST(net, httpGetHTTP) {
  Url url("http://www.google.com");
  const auto reply = Net::httpGet(url);
}

// TODO: Disabled until https support is added
TEST(net, DISABLED_httpGetHTTPS) {
  Url url("https://httpstat.us");
  const auto reply = Net::httpGet(url);
}