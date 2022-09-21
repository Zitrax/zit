// -*- mode:c++; c-basic-offset : 2; -*-
#include "messages.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <ios>
#include <optional>

#include "spdlog/spdlog.h"
#include "string_utils.hpp"

using namespace std;

namespace zit {

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

static std::ostream& operator<<(std::ostream& os, const peer_wire_id& id) {
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
  }
  return os;
}

/**
 * BitTorrent handshake message.
 */
class HandshakeMsg {
 public:
  HandshakeMsg(bytes reserved,
               Sha1 info_hash,
               string peer_id,
               size_t consumed,
               Bitfield bf = {})
      : m_reserved(move(reserved)),
        m_info_hash(info_hash),
        m_peer_id(move(peer_id)),
        m_bitfield(move(bf)),
        m_consumed(consumed) {}

  [[nodiscard]] auto getReserved() const { return m_reserved; }
  [[nodiscard]] auto getInfoHash() const { return m_info_hash; }
  [[nodiscard]] auto getPeerId() const { return m_peer_id; }
  [[nodiscard]] auto getBitfield() const { return m_bitfield; }
  [[nodiscard]] auto getConsumed() const { return m_consumed; }

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
    Sha1 info_hash = Sha1::fromBuffer(msg, 28);
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
      auto end = 73 + len - 1;
      if (end > msg.size()) {
        console->debug("Wait for more handshake data...");
        return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, 0);
      }
      Bitfield bf(bytes(&msg[73], &msg[73 + len - 1]));
      console->debug("Handshake: {}", bf);
      // Consume the parsed part, the caller have to deal with the rest
      return make_optional<HandshakeMsg>(reserved, info_hash, peer_id,
                                         73 + len - 1, bf);
    }

    return make_optional<HandshakeMsg>(reserved, info_hash, peer_id,
                                       msg.size());
  }

 private:
  bytes m_reserved;
  Sha1 m_info_hash;
  string m_peer_id;
  Bitfield m_bitfield;
  size_t m_consumed;
};

// Print start of buffer in hex format
static string debugMsg(const bytes& msg, size_t len = 100) {
  stringstream ss;
  ss.fill('0');
  ss << hex;
  for (decltype(len) i = 0; i < min(msg.size(), len); ++i) {
    ss << setw(2) << int(msg[i]) << " ";
  }
  return ss.str();
}

size_t Message::parse(PeerConnection& connection) {
  auto handshake = HandshakeMsg::parse(m_msg);
  auto& peer = connection.peer();
  if (handshake) {
    m_logger->info("{}: Handshake of size {}", connection.peer().str(),
                   m_msg.size());
    auto consumed = handshake->getConsumed();
    if (!peer.verify_info_hash(handshake->getInfoHash())) {
      // TODO: Spec say that we should drop the connection
      m_logger->warn("Unexpected info_hash");
      return consumed;
    }
    auto bf = handshake->getBitfield();
    if (bf.size() && consumed) {
      peer.set_remote_pieces(bf);
    }
    if (peer.is_listening()) {
      peer.handshake();
    }
    peer.report_bitfield();
    peer.set_am_interested(true);
    return consumed;
  }

  if (m_msg.size() >= 4) {
    auto len = big_endian(m_msg);
    m_logger->debug("{}: Incoming length = {}, message/buffer size = {}",
                    connection.peer().str(), len, m_msg.size());
    if (len + 4 > m_msg.size()) {
      // Not a full message - return and await more data
      return 0;
    }
    if (len == 0 && m_msg.size() >= 4) {
      m_logger->debug("{}: Keep Alive", connection.peer().str());
      return 4;
    }
    if (len > 0 && m_msg.size() >= 5) {
      auto id = to_peer_wire_id(m_msg[4]);
      m_logger->debug("{}: Received: {}", connection.peer().str(), id);
      switch (id) {
        case peer_wire_id::CHOKE:
          peer.set_choking(true);
          return len + 4;
        case peer_wire_id::UNCHOKE:
          peer.set_choking(false);
          return len + 4;
        case peer_wire_id::PIECE: {
          auto index = big_endian(m_msg, 5);
          auto offset = big_endian(m_msg, 9);
          // FIXME: Optimization:
          //        Could pass the range instead of copying the data
          auto start = m_msg.begin() + 13;
          auto end = start + (len - 9);
          auto data = bytes(start, end);
          peer.set_block(index, offset, data);
        }
          return len + 4;
        case peer_wire_id::INTERESTED:
          peer.set_interested(true);
          return len + 4;
        case peer_wire_id::NOT_INTERESTED:
          peer.set_interested(false);
          return len + 4;
        case peer_wire_id::HAVE: {
          try {
            auto have_id = big_endian(m_msg, 5);
            peer.have(have_id);
          } catch (const std::out_of_range&) {
            // Seen cases of too short messages here, for when it happen again
            // print the full message.
            m_logger->error("{}: Message: {}", connection.peer().str(),
                            debugMsg(m_msg));
            throw;
          }

          return 9;
        }
        case peer_wire_id::BITFIELD: {
          auto start = m_msg.begin() + 5;
          auto end = start + len - 1;
          auto bf = Bitfield(bytes(start, end));
          peer.set_remote_pieces(bf);
          m_logger->debug("{}: {}", connection.peer().str(), bf);
          return len + 4;
        }
        case peer_wire_id::REQUEST: {
          auto index = big_endian(m_msg, 5);
          auto begin = big_endian(m_msg, 9);
          auto length = big_endian(m_msg, 13);
          peer.request(index, begin, length);
          return len + 4;
        }
        // Currently unsupported messages:

        // Cancel is supposed to be used at the endgame to speed up the last
        // pieces.
        case peer_wire_id::CANCEL:
        // The port message is sent by newer versions of the Mainline that
        // implements a DHT tracker. The listen port is the port this peer's DHT
        // node is listening on. This peer should be inserted in the local
        // routing table (if DHT tracker is supported).
        case peer_wire_id::PORT:
        case peer_wire_id::UNKNOWN:
          break;
      }
      m_logger->warn("{}: {} is unhandled", connection.peer().str(), id);
      return m_msg.size();
    }
  }

  m_logger->warn("{}: Unknown message of length {} ({})",
                 connection.peer().str(), m_msg.size(), debugMsg(m_msg));
  return m_msg.size();
}

}  // namespace zit
