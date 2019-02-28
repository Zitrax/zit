// -*- mode:c++; c-basic-offset : 2; -*-
#include "messages.h"

#include <algorithm>
#include <cstring>
#include <optional>

#include "spdlog/spdlog.h"
#include "string_utils.h"

using namespace std;

namespace zit {

/**
 * Convenience wrapper for std::all_of.
 *
 * (To be replaced with ranges in C++20)
 */
template <class Container, class UnaryPredicate>
static bool all_of(Container c, UnaryPredicate p) {
  return std::all_of(c.begin(), c.end(), p);
}

template <typename T>
static peer_wire_id to_peer_wire_id(const T& t) {
  auto val = numeric_cast<pwid_t>(t);
  switch (val) {
    case static_cast<pwid_t>(peer_wire_id::CHOKE):
      return peer_wire_id::CHOKE;
    case static_cast<pwid_t>(peer_wire_id::UNCHOKE):
      return peer_wire_id::UNCHOKE;
    case static_cast<pwid_t>(peer_wire_id::INTERESTED):
      return peer_wire_id::INTERESTED;
    case static_cast<pwid_t>(peer_wire_id::NOT_INTERESTED):
      return peer_wire_id::NOT_INTERESTED;
    case static_cast<pwid_t>(peer_wire_id::HAVE):
      return peer_wire_id::HAVE;
    case static_cast<pwid_t>(peer_wire_id::BITFIELD):
      return peer_wire_id::BITFIELD;
    case static_cast<pwid_t>(peer_wire_id::REQUEST):
      return peer_wire_id::REQUEST;
    case static_cast<pwid_t>(peer_wire_id::PIECE):
      return peer_wire_id::PIECE;
    case static_cast<pwid_t>(peer_wire_id::CANCEL):
      return peer_wire_id::CANCEL;
    case static_cast<pwid_t>(peer_wire_id::PORT):
      return peer_wire_id::PORT;
    default:
      spdlog::get("console")->error("Unknown id = {}", val);
      return peer_wire_id::UNKNOWN;
  }
}

std::ostream& operator<<(std::ostream& os, const peer_wire_id& id) {
  switch (id) {
    case peer_wire_id::CHOKE:
      os << "CHOKE";
      break;
    case peer_wire_id::UNCHOKE:
      os << "UNCHOKE";
      break;
    case peer_wire_id::INTERESTED:
      os << "INTERESTED";
      break;
    case peer_wire_id::NOT_INTERESTED:
      os << "NOT_INTERESTED";
      break;
    case peer_wire_id::HAVE:
      os << "HAVE";
      break;
    case peer_wire_id::BITFIELD:
      os << "BITFIELD";
      break;
    case peer_wire_id::REQUEST:
      os << "REQUEST";
      break;
    case peer_wire_id::PIECE:
      os << "PIECE";
      break;
    case peer_wire_id::CANCEL:
      os << "CANCEL";
      break;
    case peer_wire_id::PORT:
      os << "PORT";
      break;
    case peer_wire_id::UNKNOWN:
      os << "UNKNOWN";
      break;
    default:
      os << "<peer_wire_id:" << id << ">";
      break;
  }
  return os;
}

/**
 * BitTorrent handshake message.
 */
class HandshakeMsg {
 public:
  HandshakeMsg(bytes reserved, Sha1 info_hash, string peer_id, Bitfield bf = {})
      : m_reserved(move(reserved)),
        m_info_hash(info_hash),
        m_peer_id(move(peer_id)),
        m_bitfield(move(bf)) {}

  [[nodiscard]] auto getReserved() const { return m_reserved; }
  [[nodiscard]] auto getInfoHash() const { return m_info_hash; }
  [[nodiscard]] auto getPeerId() const { return m_peer_id; }
  [[nodiscard]] auto getBitfield() const { return m_bitfield; }

  /**
   * Parse bytes and return handshake message if it is one.
   */
  static optional<HandshakeMsg> parse(const bytes& msg) {
    if (msg.size() < 68) {  // BitTorrent messages are minimum 68 bytes long
      return {};
    }
    if (memcmp("\x13"
               "BitTorrent protocol",
               msg.data(), 20) != 0) {
      return {};
    }
    bytes reserved(&msg[20], &msg[28]);
    Sha1 info_hash = Sha1::fromBytes(msg, 28);
    string peer_id = from_bytes(msg, 48, 68);

    auto console = spdlog::get("console");

    if (msg.size() > 68) {
      if (msg.size() < 73) {
        console->error("Invalid handshake length: {}", msg.size());
        return {};
      }
      if (to_peer_wire_id(msg[72]) != peer_wire_id::BITFIELD) {
        console->error("Expected bitfield id ({}) but got: {}",
                       static_cast<pwid_t>(peer_wire_id::BITFIELD),
                       static_cast<uint8_t>(msg[72]));
        return {};
      }
      // 4-byte big endian
      auto len = big_endian(msg, 68);
      Bitfield bf(bytes(&msg[73], &msg[73 + len - 1]));
      console->info("{}", bf);
      return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, bf);
    }

    return make_optional<HandshakeMsg>(reserved, info_hash, peer_id);
  }

 private:
  bytes m_reserved;
  Sha1 m_info_hash;
  string m_peer_id;
  Bitfield m_bitfield;
};

bool Message::parse(PeerConnection& connection) {
  auto handshake = HandshakeMsg::parse(m_msg);
  if (handshake) {
    m_logger->info("Handshake");
    connection.peer().set_am_interested(true);
    auto bf = handshake.value().getBitfield();
    if (bf.size()) {
      connection.peer().set_remote_pieces(bf);
    }
    return true;
  }

  if (m_msg.size() >= 4) {
    // Read 4 byte length
    auto len = big_endian(m_msg);
    m_logger->info("Incoming length = {}, message size = {}", len,
                   m_msg.size());
    if (len > m_msg.size() + 4) {
      // Not a full message - return and await more data
      return false;
    }
    if (len == 0 && m_msg.size() == 4) {
      m_logger->info("Keep Alive");
    } else if (m_msg.size() >= 5) {
      auto id = to_peer_wire_id(m_msg[4]);
      m_logger->info("Received: {}", id);
      switch (id) {
        case peer_wire_id::CHOKE:
          break;
        case peer_wire_id::UNCHOKE:
          connection.peer().set_choking(false);
          return true;
        case peer_wire_id::PIECE: {
          auto index = big_endian(m_msg, 5);
          auto offset = big_endian(m_msg, 9);
          // FIXME: Optimization:
          //        Could pass the range instead of copying the data
          auto data = bytes(m_msg.begin() + 13, m_msg.end());
          connection.peer().set_block(index, offset, data);
        }
          return true;
        case peer_wire_id::INTERESTED:
        case peer_wire_id::NOT_INTERESTED:
        case peer_wire_id::HAVE:
        case peer_wire_id::BITFIELD:
        case peer_wire_id::REQUEST:
        case peer_wire_id::CANCEL:
        case peer_wire_id::PORT:
        case peer_wire_id::UNKNOWN:
          break;
      }
      m_logger->warn("{} is unhandled", id);
      return true;
    }
  }

  m_logger->warn("Unknown message of length {}", m_msg.size());
  return true;
}

bool Message::is_keepalive(const bytes& msg) {
  return msg.size() == 4 && all_of(msg, [](byte b) { return b == 0_b; });
}

}  // namespace zit
