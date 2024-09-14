// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
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

/** For fmt */
std::string format_as(const peer_wire_id& id);

using pwid_t = std::underlying_type_t<peer_wire_id>;

// BitTorrent messages are minimum 68 bytes long
constexpr size_t MIN_BT_MSG_LENGTH{68};

/**
 * BitTorrent handshake message.
 */
class HandshakeMsg {
 public:
  HandshakeMsg(bytes reserved,
               Sha1 info_hash,
               std::string peer_id,
               size_t consumed,
               Bitfield bf = {})
      : m_reserved(std::move(reserved)),
        m_info_hash(info_hash),
        m_peer_id(std::move(peer_id)),
        m_bitfield(std::move(bf)),
        m_consumed(consumed) {}

  [[nodiscard]] auto getReserved() const { return m_reserved; }
  [[nodiscard]] auto getInfoHash() const { return m_info_hash; }
  [[nodiscard]] auto getPeerId() const { return m_peer_id; }
  [[nodiscard]] auto getBitfield() const { return m_bitfield; }
  [[nodiscard]] auto getConsumed() const { return m_consumed; }

  /**
   * Parse bytes and return handshake message if it is one.
   */
  static std::optional<HandshakeMsg> parse(const bytes& msg);

 private:
  bytes m_reserved;
  Sha1 m_info_hash;
  std::string m_peer_id;
  Bitfield m_bitfield;
  size_t m_consumed;
};

/**
 * Incoming torrent message from the network. Main entry point is
 * Message::parse().
 */
class Message {
 public:
  explicit Message(const bytes& msg) : m_msg(msg) {}

  /**
   * Parse the message and return the number of bytes consumed.
   */
  size_t parse(PeerConnection& connection);

 private:
  const bytes& m_msg;
};

}  // namespace zit
