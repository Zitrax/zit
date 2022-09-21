// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "peer.hpp"

#include "spdlog/spdlog.h"

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

/**
 * Incoming torrent message from the network. Main entry point is
 * Message::parse().
 */
class Message {
 public:
  explicit Message(const bytes& msg)
      : m_msg(msg), m_logger(spdlog::get("console")) {}

  /**
   * Parse the message and return the number of bytes consumed.
   */
  size_t parse(PeerConnection& connection);

 private:
  const bytes& m_msg;
  std::shared_ptr<spdlog::logger> m_logger;
};

}  // namespace zit
