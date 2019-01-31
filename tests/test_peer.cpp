// -*- mode:c++; c-basic-offset : 2; -*-

#include "gtest/gtest.h"
#include "peer.h"

using namespace zit;

TEST(peer, construct) {
  Url localhost("tcp", "localhost", 35253);
  Peer peer(localhost);
}

TEST(peer, handshake) {
  Url localhost("tcp", "localhost", 35253);
  Peer peer(localhost);
  // FIXME: possibly mock this
  // peer.handshake("a");
}
