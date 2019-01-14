// -*- mode:c++; c-basic-offset : 2; -*-
#include "messages.h"

#include <algorithm>
#include <cstring>
#include <optional>

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
  UNKNOWN = numeric_limits<uint8_t>::max()
};

using pwid_t = underlying_type_t<peer_wire_id>;

template <typename T>
static peer_wire_id to_peer_wire_id(const T& t) {
  switch (numeric_cast<pwid_t>(t)) {
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
  }
  return peer_wire_id::UNKNOWN;
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
  HandshakeMsg(bytes reserved, sha1 info_hash, string peer_id, bitfield bf = {})
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
    sha1 info_hash = sha1::from_bytes(msg, 28);
    string peer_id = from_bytes(msg, 48, 68);

    if (msg.size() > 68) {
      if (msg.size() < 73) {
        cerr << "Invalid handshake length: " << msg.size() << "\n";
        return {};
      }
      if (to_peer_wire_id(msg[72]) != peer_wire_id::BITFIELD) {
        cerr << "Expected bitfield id ("
             << static_cast<pwid_t>(peer_wire_id::BITFIELD)
             << ") but got: " << static_cast<uint8_t>(msg[72]) << "\n";
        return {};
      }
      // 4-byte big endian
      auto len = big_endian(msg, 68);
      bitfield bf(bytes(&msg[73], &msg[73 + len - 1]));
      cout << bf;
      return make_optional<HandshakeMsg>(reserved, info_hash, peer_id, bf);
    }

    return make_optional<HandshakeMsg>(reserved, info_hash, peer_id);
  }

 private:
  bytes m_reserved;
  sha1 m_info_hash;
  string m_peer_id;
  bitfield m_bitfield;
};

void Message::parse(PeerConnection& connection) {
  auto handshake = HandshakeMsg::parse(m_msg);
  if (handshake) {
    cout << "Handshake\n";
    // Send INTERESTED
    string interested = {0, 0, 0, 1,
                         static_cast<pwid_t>(peer_wire_id::INTERESTED)};
    stringstream hs;
    hs.write(interested.c_str(),
             numeric_cast<std::streamsize>(interested.length()));
    connection.write(hs.str());
    return;
  }

  if (m_msg.size() >= 4) {
    // Read 4 byte length
    auto len = big_endian(m_msg);
    if (len == 0 && m_msg.size() == 4) {
      cout << "Keep Alive\n";
    } else if (len == 1 && m_msg.size() >= 5) {
      auto id = to_peer_wire_id(m_msg[4]);
      cout << id << "\n";
      switch (id) {
        case peer_wire_id::CHOKE:
        case peer_wire_id::UNCHOKE:
        case peer_wire_id::INTERESTED:
        case peer_wire_id::NOT_INTERESTED:
        case peer_wire_id::HAVE:
        case peer_wire_id::BITFIELD:
        case peer_wire_id::REQUEST:
        case peer_wire_id::PIECE:
        case peer_wire_id::CANCEL:
        case peer_wire_id::PORT:
        case peer_wire_id::UNKNOWN:
          break;
      }
      return;
    }
  }

  cout << "Unknown message of length " << m_msg.size() << "\n";
}

bool Message::is_keepalive(const bytes& msg) {
  return msg.size() == 4 && all_of(msg, [](byte b) { return b == 0_b; });
}

}  // namespace zit
