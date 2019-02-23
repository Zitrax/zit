// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "peer.h"

namespace zit {
enum class peer_wire_id : uint8_t {
  CHOKE = 0,
  UNCHOKE = 1,
  INTERESTED = 2,
  NOT_INTERESTED = 3,
  HAVE = 4,
  BITFIELD = 5,
  REQUEST = 6,
  PIECE = 7,
  CANCEL = 8,
  PORT = 9,
  UNKNOWN = std::numeric_limits<uint8_t>::max()
};

using pwid_t = std::underlying_type_t<peer_wire_id>;

class Message {
 public:
  explicit Message(const bytes& msg) : m_msg(msg) {}

  bool parse(PeerConnection& connection);

  bool is_keepalive(const bytes& msg);

 private:
  const bytes& m_msg;
};

}  // namespace zit
