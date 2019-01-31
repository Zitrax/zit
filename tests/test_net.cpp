// -*- mode:c++; c-basic-offset : 2; -*-
#include <arpa/inet.h>
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
