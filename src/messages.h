// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "peer.h"

namespace zit {

class Message {
 public:
  explicit Message(const bytes& msg) : m_msg(msg) {}

  void parse(PeerConnection& connection);

  bool is_keepalive(const bytes& msg);

 private:
  const bytes& m_msg;
};

}  // namespace zit
