// -*- mode:c++; c-basic-offset : 2; -*-
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "net.hpp"
#include "process.hpp"
#include "random.hpp"

#include <fmt/format.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <asio.hpp>
#include <filesystem>
#include <memory>

using asio::detail::socket_ops::host_to_network_short;

using ::testing::ElementsAreArray;

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
  Url url(urlstr, Url::Binary{true});
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
  EXPECT_EQ(url1.authority(), "torrent.ubuntu.com");

  Url url2("http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(url2.scheme(), "http");
  EXPECT_EQ(url2.host(), "torrent.ubuntu.com");
  EXPECT_EQ(url2.port(), 6969);
  EXPECT_EQ(url2.service(), "6969");
  EXPECT_EQ(url2.path(), "/announce");
  EXPECT_EQ(url2.authority(), "torrent.ubuntu.com:6969");

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
  EXPECT_EQ(url3.authority(), "torrent.ubuntu.com");
}

TEST(net, url_equality) {
  Url google_url("http://www.google.com");
  Url google_url2("http://www.google.com");
  EXPECT_EQ(google_url, google_url);
  EXPECT_EQ(google_url, google_url2);

  Url amazon_url("http://www.amazon.com");
  EXPECT_NE(google_url, amazon_url);
}

TEST(net, url_resolve) {
  Url url("http://127.0.0.1/");
  url.resolve();
  EXPECT_FALSE(url.host().empty());
  EXPECT_NE(url.host(), "127.0.0.1");
}

TEST(net, httpGetHTTP) {
  Url url("http://www.google.com");
  const auto reply = Net::httpGet(url);
}

TEST(net, httpGetHTTPS) {
  Url url("https://www.google.com");
  const auto reply = Net::httpGet(url);
  Url url2("https://www.google.com:443");
  const auto reply2 = Net::httpGet(url2);
}

TEST(net, chunkedTransfer) {
  Url url("https://jigsaw.w3.org/HTTP/ChunkedScript");

  constexpr auto sep =
      "------------------------------------------------------------------------"
      "-\n";
  constexpr auto line =
      "01234567890123456789012345678901234567890123456789012345678901"
      "234567890\n";
  std::stringstream expected;
  expected << sep;
  for (int i = 0; i < 1000; i++) {
    expected << line;
  }

  auto [headers, response] = Net::httpGet(url);
  const auto pos = response.find(sep);
  ASSERT_TRUE(pos != std::string::npos);
  response.erase(0, pos);
  EXPECT_EQ(expected.str(), response);
}

/**
 * Multiple udp tests might run in parallell processes. We are listening on
 * random ports so there is still a slim chance of failure if the same port
 * is picked.
 */
class UDP : public ::testing::Test {
 public:
  [[nodiscard]] auto port() const { return m_port; }

 protected:
  void SetUp() override {
    const auto udp_server_path =
        (std::filesystem::path(DATA_DIR) / ".." / "udp_server.py")
            .lexically_normal();
    echo_server = std::make_unique<zit::Process>(
        "udp_echo",
        std::vector<std::string>{"python", udp_server_path.generic_string(),
                                 std::to_string(m_port)});
  }

  void TearDown() override {
    if (echo_server) {
      echo_server->terminate();
    }
  }

 private:
  std::unique_ptr<zit::Process> echo_server{};
  static int port_start;
  int m_port{random_value(10000, 20000)};
};

TEST_F(UDP, request_ip) {
  Url url(fmt::format("udp://127.0.0.1:{}", std::to_string(port())));

  const bytes message{'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b};
  const auto reply = Net::udpRequest(url, message);
  ASSERT_THAT(message, ElementsAreArray(reply));
}

TEST_F(UDP, request_named_host) {
  Url url(fmt::format("udp://localhost:{}", std::to_string(port())));

  const bytes message{'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b};
  const auto reply = Net::udpRequest(url, message);
  ASSERT_THAT(message, ElementsAreArray(reply));
}
