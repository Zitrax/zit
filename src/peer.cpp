// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.h"

#include "string_utils.h"
#include "types.h"

#include <asio.hpp>

#include <iostream>
#include <memory>
#include <optional>
#include "messages.h"

using asio::ip::tcp;
using namespace std;
using std::placeholders::_1;
using std::placeholders::_2;

namespace zit {

// Note that since we are using asio without boost
// we use std::bind, std::shared_ptr, etc... which
// differs slightly from the boost examples.

class peer_connection {
 public:
  peer_connection(Peer& peer,
                  asio::io_service& io_service,
                  unsigned short port_num)
      : peer_(peer),
        resolver_(io_service),
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

  void handle_response(const asio::error_code& err) {
    cout << __PRETTY_FUNCTION__ << endl;
    if (!err) {
      if (response_.size()) {
        bytes response(response_.size());
        buffer_copy(asio::buffer(response), response_.data());
        response_.consume(response_.size());
        Message msg(peer_, response);
        msg.parse();
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
  Peer& peer_;
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
    connection = make_unique<peer_connection>(*this, io_service, port);
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
