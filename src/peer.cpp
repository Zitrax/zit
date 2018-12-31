// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.h"

#include "bitfield.h"
#include "string_utils.h"
#include "types.h"

#include <asio.hpp>

#include <iostream>
#include <memory>
#include <optional>

using asio::ip::tcp;
using namespace std;
using std::placeholders::_1;
using std::placeholders::_2;

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

// Note that since we are using asio without boost
// we use std::bind, std::shared_ptr, etc... which
// differs slightly from the boost examples.

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

/**
 * BitTorrent handshake message.
 */
class handshake_msg {
 public:
  handshake_msg(bytes reserved,
                sha1 info_hash,
                string peer_id,
                bitfield bf = {})
      : m_reserved(move(reserved)),
        m_info_hash(info_hash),
        m_peer_id(move(peer_id)),
        m_bitfield(move(bf)) {}

  auto reserved() const { return m_reserved; }
  auto info_hash() const { return m_info_hash; }
  auto peer_id() const { return m_peer_id; }

  /**
   * Parse bytes and return handshake message if it is one.
   */
  static optional<handshake_msg> parse(const bytes& msg) {
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
      bitfield bf(bytes(&msg[73], &msg[73 + len]));
      return make_optional<handshake_msg>(reserved, info_hash, peer_id, bf);
    } else {
      return make_optional<handshake_msg>(reserved, info_hash, peer_id);
    }
  }

 private:
  bytes m_reserved;
  sha1 m_info_hash;
  string m_peer_id;
  bitfield m_bitfield;
};

class peer_connection {
 public:
  peer_connection(asio::io_service& io_service, unsigned short port_num)
      : resolver_(io_service),
        socket_(io_service, tcp::endpoint(tcp::v4(), port_num)) {
    // TODO: This does not seem to help
    asio::socket_base::reuse_address option(true);
    socket_.set_option(option);
  }

  void write(const Url& url, const string& msg) {
    ostream request_stream(&request_);
    request_stream << msg;

    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    tcp::resolver::query query(url.host(), to_string(url.port()));
    resolver_.async_resolve(
        query, bind(&peer_connection::handle_resolve, this, _1, _2));
  }

 private:
  void handle_resolve(const asio::error_code& err,
                      tcp::resolver::iterator endpoint_iterator) {
    cout << __PRETTY_FUNCTION__ << endl;
    if (!err) {
      // Attempt a connection to the first endpoint in the list. Each endpoint
      // will be tried until we successfully establish a connection.
      tcp::endpoint endpoint = *endpoint_iterator;
      socket_.async_connect(endpoint, bind(&peer_connection::handle_connect,
                                           this, _1, ++endpoint_iterator));
    } else {
      cout << "Error: " << err.message() << "\n";
    }
  }

  void handle_connect(const asio::error_code& err,
                      tcp::resolver::iterator endpoint_iterator) {
    cout << __PRETTY_FUNCTION__ << endl;
    if (!err) {
      // The connection was successful. Send the request.
      asio::async_write(socket_, request_,
                        bind(&peer_connection::handle_response, this, _1));
    } else if (endpoint_iterator != tcp::resolver::iterator()) {
      // The connection failed. Try the next endpoint in the list.
      socket_.close();
      tcp::endpoint endpoint = *endpoint_iterator;
      socket_.async_connect(endpoint, bind(&peer_connection::handle_connect,
                                           this, _1, ++endpoint_iterator));
    } else {
      cout << "Error: " << err.message() << "\n";
    }
  }

  /**
   * A keepalive is a message of zeroes with length 4 bytes.
   */
  bool is_keepalive(const bytes& msg) {
    return msg.size() == 4 && all_of(msg, [](byte b) { return b == 0_b; });
  }

  void handle_response(const asio::error_code& err) {
    cout << __PRETTY_FUNCTION__ << endl;
    if (!err) {
      if (response_.size()) {
        bytes response(response_.size());
        buffer_copy(asio::buffer(response), response_.data());
        response_.consume(response_.size());

        if (is_keepalive(response)) {
          cout << "Keep Alive\n";
        } else if (handshake_msg::parse(response)) {
          cout << "Handshake\n";
        } else {
          cout << "Unknown message of length " + to_string(response.size()) +
                      "\n";
        }
        cout.flush();
      }

      // Read remaining data until EOF.
      asio::async_read(socket_, response_, asio::transfer_at_least(1),
                       bind(&peer_connection::handle_response, this, _1));
    } else if (err != asio::error::eof) {
      cout << "Error: " << err.message() << "\n";
    } else {
      cout << "EOF\n";
    }
  }

  asio::streambuf request_{};
  tcp::resolver resolver_;
  asio::streambuf response_{};
  tcp::socket socket_;
};

void Peer::handshake(const sha1& info_hash) {
  // The handshake should contain:
  // <pstrlen><pstr><reserved><info_hash><peer_id>

  // handshake -> <pstrlen><pstr><reserved><info_hash><peer_id>
  // Q. What is the reply to the handshake?
  // A. Seem to be a handshake reply with bitfield (at least by vuze)

  // Next comes messages that might be of types:
  // * keep_alive
  // * choke
  // * unchoke
  // * interesetd
  // * not interested
  // * have
  // * bitfield
  // * request
  // * piece
  // * cancel
  // * port

  // After the fixed headers come eight reserved bytes, which are all zero in
  // all current implementations.
  // However I see that vuze do not use all zeroes.
  const string reserved = {0, 0, 0, 0, 0, 0, 0, 0};

  stringstream hs;
  hs << static_cast<char>(19) << "BitTorrent protocol";
  hs.write(reserved.c_str(), numeric_cast<std::streamsize>(reserved.length()));
  hs << info_hash.str() << "abcdefghijklmnopqrst";  // FIXME: Use proper peer-id

  auto port = 20000;  // FIXME: configurable port

  // Assume we need to start listening immediately, then send handshake
  asio::io_service io_service;
  auto connection = unique_ptr<peer_connection>();
  try {
    connection = make_unique<peer_connection>(io_service, port);
  } catch (const asio::system_error& err) {
    throw_with_nested(runtime_error("Creating peer connection to " +
                                    m_url.authority() + " from port " +
                                    to_string(port)));
  }
  connection->write(m_url, hs.str());
  io_service.run();

  // First test ...
}

}  // namespace zit
