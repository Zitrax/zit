// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "peer.h"

namespace zit {

class Message {
 public:
  Message(Peer& peer, const bytes& msg) : m_peer(peer), m_msg(msg) {}

  void parse();

  bool is_keepalive(const bytes& msg);

 private:
  Peer& m_peer;
  const bytes& m_msg;
};

}  // namespace zit
