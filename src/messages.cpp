// -*- mode:c++; c-basic-offset : 2; -*-
#include "messages.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ios>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>

#include "bitfield.hpp"
#include "logger.hpp"
#include "peer.hpp"
#include "sha1.hpp"
#include "string_utils.hpp"
#include "torrent.hpp"
#include "types.hpp"

using namespace std;

namespace zit {

// Need to be in zit for fmt
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
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

std::string format_as(const peer_wire_id& id) {
  std::stringstream ss;
  ss << id;
  return ss.str();
}

namespace {

template <typename T>
peer_wire_id to_peer_wire_id(const T& t) {
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
      logger()->error("Unknown id = {}", val);
      return peer_wire_id::UNKNOWN;
  }
}

// Print start of buffer in hex format
string debugMsg(span<const byte> msg) {
  stringstream ss;
  ss.fill('0');
  ss << hex;
  for (auto b : msg) {
    ss << setw(2) << int(b) << " ";
  }
  return ss.str();
}

}  // namespace

optional<HandshakeMsg> HandshakeMsg::parse(const bytes& msg) {
  if (msg.size() < MIN_BT_MSG_LENGTH) {
    return {};
  }

  constexpr std::array<byte, 20> BT_START{
      byte{0x13}, 'B'_b, 'i'_b, 't'_b, 'T'_b, 'o'_b, 'r'_b,
      'r'_b,      'e'_b, 'n'_b, 't'_b, ' '_b, 'p'_b, 'r'_b,
      'o'_b,      't'_b, 'o'_b, 'c'_b, 'o'_b, 'l'_b};

  auto it =
      std::search(msg.begin(), msg.end(), BT_START.begin(), BT_START.end());
  if (it == msg.end()) {
    logger()->debug("No handshake match:\nGot: {}\nExp: {}",
                    debugMsg(span(msg.begin(), BT_START.size())),
                    debugMsg(BT_START));
    return {};
  }

  if (it != msg.begin()) {
    logger()->debug("Found BT start at {}", std::distance(msg.begin(), it));
    return HandshakeMsg::parse(bytes{it, msg.end()});
  }

  if (memcmp("\x13"
             "BitTorrent protocol",
             msg.data(), 20) != 0) {
    // debug log bytes
    logger()->debug("MSG: {}", debugMsg(msg));
    return {};
  }
  bytes reserved(&msg[20], &msg[28]);
  Sha1 info_hash = Sha1::fromBuffer(msg, 28);
  string peer_id = from_bytes(msg, 48, 68);

  // Handle optional bitfield
  if (msg.size() > MIN_BT_MSG_LENGTH) {
    if (msg.size() < 73) {
      logger()->error("Invalid handshake length: {}", msg.size());
      return {};
    }
    if (to_peer_wire_id(msg[72]) != peer_wire_id::BITFIELD) {
      logger()->error("Expected bitfield id ({}) but got: {}",
                      static_cast<pwid_t>(peer_wire_id::BITFIELD),
                      static_cast<uint8_t>(msg[72]));
      return {};
    }
    // 4-byte big endian
    auto len = from_big_endian<uint32_t>(msg, 68);
    auto end = 73 + len - 1;
    if (end > msg.size()) {
      logger()->debug("Wait for more handshake data...");
      return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, 0);
    }
    Bitfield bf(bytes(std::next(msg.begin(), 73), std::next(msg.begin(), end)));
    logger()->debug("Handshake: {}", bf);
    // Consume the parsed part, the caller have to deal with the rest
    return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, end, bf);
  }

  return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, msg.size());
}

size_t Message::parse(PeerConnection& connection) {
  auto handshake = HandshakeMsg::parse(m_msg);
  auto& peer = connection.peer();
  peer.update_activity();
  if (handshake) {
    auto consumed = handshake->getConsumed();
    if (consumed) {
      logger()->info("{}: Got handshake of size {}", peer.str(), m_msg.size());
      if (!peer.verify_info_hash(handshake->getInfoHash())) {
        // TODO: Spec say that we should drop the connection
        logger()->warn("Unexpected info_hash");
        return consumed;
      }
      const auto bf = handshake->getBitfield();
      if (bf.size()) {
        logger()->info("{}: Has {}/{} pieces", peer.str(), bf.count(),
                       peer.torrent().pieces().size());
        peer.set_remote_pieces(bf);
      }
      if (peer.is_listening()) {
        peer.handshake();
      }
      peer.report_bitfield();
      peer.set_am_interested(true);
    } else {
      logger()->debug("{}: Got handshake part of size {}", peer.str(),
                      m_msg.size());
    }
    return consumed;
  }

  if (m_msg.size() >= 4) {
    auto len = from_big_endian<uint32_t>(m_msg);
    logger()->debug("{}: Incoming length = {}, message/buffer size = {}",
                    peer.str(), len, m_msg.size());
    if (len + 4 > m_msg.size()) {
      // Not a full message - return and await more data
      return 0;
    }
    assert(m_msg.size() >= 4);
    if (len == 0) {
      logger()->debug("{}: Keep Alive", peer.str());
      return 4;
    }
    if (m_msg.size() >= 5) {
      auto id = to_peer_wire_id(m_msg[4]);
      logger()->debug("{}: Received: {}", peer.str(), id);
      switch (id) {
        case peer_wire_id::CHOKE:
          peer.set_choking(true);
          return len + 4;
        case peer_wire_id::UNCHOKE:
          peer.set_choking(false);
          return len + 4;
        case peer_wire_id::PIECE: {
          const auto index = from_big_endian<uint32_t>(m_msg, 5);
          const auto offset = from_big_endian<uint32_t>(m_msg, 9);
          const auto start = m_msg.begin() + 13;
          const auto end = start + (len - 9);
          peer.set_block(index, offset, {start, end});
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
            auto have_id = from_big_endian<uint32_t>(m_msg, 5);
            peer.have(have_id);
          } catch (const std::out_of_range&) {
            // Seen cases of too short messages here, for when it happen again
            // print the full message.
            logger()->error("{}: Message: {}", peer.str(), debugMsg(m_msg));
            throw;
          }

          return 9;
        }
        case peer_wire_id::BITFIELD: {
          auto start = std::next(m_msg.begin(), 5);
          auto end = std::next(start, len - 1);
          auto bf = Bitfield(bytes(start, end));
          peer.set_remote_pieces(bf);
          logger()->debug("{}: {}", peer.str(), bf);
          return len + 4;
        }
        case peer_wire_id::REQUEST: {
          auto index = from_big_endian<uint32_t>(m_msg, 5);
          auto begin = from_big_endian<uint32_t>(m_msg, 9);
          auto length = from_big_endian<uint32_t>(m_msg, 13);
          peer.request(index, begin, length);
          return len + 4;
        }
        // Currently unsupported messages:

        // Cancel is supposed to be used at the endgame to speed up the last
        // pieces.
        case peer_wire_id::CANCEL:
        // The port message is sent by newer versions of the Mainline that
        // implements a DHT tracker. The listen port is the port this peer's
        // DHT node is listening on. This peer should be inserted in the local
        // routing table (if DHT tracker is supported).
        case peer_wire_id::PORT:
        case peer_wire_id::UNKNOWN:
          break;
      }
      logger()->warn("{}: {} is unhandled", peer.str(), id);
      return m_msg.size();
    }
  } else {
    logger()->debug("{}: Short message of length {} ({})", peer.str(),
                    m_msg.size(), debugMsg(m_msg));
    return 0;
  }

  logger()->warn("{}: Unknown message of length {} ({})", peer.str(),
                 m_msg.size(), debugMsg(m_msg));
  return m_msg.size();
}

}  // namespace zit
