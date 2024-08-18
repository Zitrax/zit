// -*- mode:c++; c-basic-offset : 2; -*-

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "asio_helpers.hpp"
#include "logger.hpp"
#include "peer.hpp"

using namespace zit;

TEST(peer, construct) {
  Url localhost("tcp", "localhost", 35253);
  // Peer peer(localhost, 0);
}

TEST(peer, handshake) {
  Url localhost("tcp", "localhost", 35253);
  // Peer peer(localhost, 0);
  // FIXME: possibly mock this
  // peer.handshake("a");
}

class MockPeer : public IConnectionUrlProvider {
 public:
  MOCK_METHOD(std::optional<Url>, url, (), (const, override));
  MOCK_METHOD(std::string, str, (), (const, override));
  MOCK_METHOD(void, disconnected, (), (override));
};

inline asio::awaitable<bool> connect(asio::io_service& io, Url url) {
  asio::ip::tcp::resolver resolver(io);
  auto results = co_await resolver.async_resolve(url.host(), url.service(),
                                                 asio::use_awaitable);
  zit::logger()->info("resolved 1");

  auto result = *results.begin();
  asio::ip::tcp::socket socket(io);
  co_await socket.async_connect(result, asio::use_awaitable);

  zit::logger()->info("{} connected to server {}", socket.local_endpoint(),
                      result.endpoint());

  io.stop();
  co_return true;
}

TEST(peer, multi_peer) {
  using namespace std::chrono_literals;

  MockPeer mockPeer;
  asio::io_service io;
  ListeningPort lport{12000};
  ConnectionPort cport{12001};
  Url url(fmt::format("http://127.0.0.1:{}", lport.get()));

  PeerConnection con1(mockPeer, io, lport, cport);
  con1.listen();
  PeerConnection con2(mockPeer, io, lport, cport);
  con2.listen();

  // Now try to connect to both PeerConnections
  auto res1 = co_spawn(io, connect(io, url), asio::use_future);
  auto res2 = co_spawn(io, connect(io, url), asio::use_future);

  io.run_for(1000ms);

  ASSERT_EQ(res1.wait_for(0s), std::future_status::ready);
  ASSERT_EQ(res2.wait_for(0s), std::future_status::ready);
  ASSERT_TRUE(res1.get());
  ASSERT_TRUE(res2.get());
}
