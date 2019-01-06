// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "peer.h"

namespace zit {

class Message {
 public:
  Message(const bytes& msg) : m_msg(msg) {}

  void parse(peer_connection& connection);

  bool is_keepalive(const bytes& msg);

 private:
  const bytes& m_msg;
};

}  // namespace zit
