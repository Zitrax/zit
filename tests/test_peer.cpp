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
