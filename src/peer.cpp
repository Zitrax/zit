// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.h"

#include "string_utils.h"

#include <iostream>
#include <memory>
#include <optional>
#include "messages.h"
#include "spdlog/spdlog.h"

using asio::ip::tcp;
using namespace std;
using std::placeholders::_1;
using std::placeholders::_2;

#ifdef WIN32
#define PRETTY_FUNCTION __FUNCSIG__
#else
#define PRETTY_FUNCTION __PRETTY_FUNCTION__
#endif  // WIN32

namespace zit {

// Note that since we are using asio without boost
// we use std::bind, std::shared_ptr, etc... which
// differs slightly from the boost examples.

PeerConnection::PeerConnection(Peer& peer,
                               asio::io_service& io_service,
                               unsigned short port_num)
    : peer_(peer),
      resolver_(io_service),
      socket_(io_service, tcp::endpoint(tcp::v4(), port_num)),
      m_logger(spdlog::get("console")) {
  // TODO: This does not seem to help
  asio::socket_base::reuse_address option(true);
  socket_.set_option(option);
}

void PeerConnection::write(const Url& url, const bytes& msg) {
  write(url, from_bytes(msg));
}

void PeerConnection::write(const bytes& msg) {
  write(peer_.url(), msg);
}

void PeerConnection::write(const std::string& msg) {
  write(peer_.url(), msg);
}

void PeerConnection::write(const Url& url, const string& msg) {
  m_logger->debug(PRETTY_FUNCTION);
  ostream request_stream(&request_);
  request_stream << msg;

  if (endpoint_ != tcp::resolver::iterator()) {
    // We have already resolved and connected
    handle_connect(asio::error_code(), endpoint_);
  } else {
    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    tcp::resolver::query query(url.host(), to_string(url.port()));
    resolver_.async_resolve(
        query, bind(&PeerConnection::handle_resolve, this, _1, _2));
  }
}

void PeerConnection::handle_resolve(const asio::error_code& err,
                                    tcp::resolver::iterator endpoint_iterator) {
  m_logger->debug(PRETTY_FUNCTION);
  if (!err) {
    // Attempt a connection to the first endpoint in the list. Each endpoint
    // will be tried until we successfully establish a connection.
    tcp::endpoint endpoint = *endpoint_iterator;
    // FIXME: Should set this after connection ok instead
    endpoint_ = endpoint_iterator;
    socket_.async_connect(endpoint, bind(&PeerConnection::handle_connect, this,
                                         _1, ++endpoint_iterator));
  } else {
    m_logger->error(err.message());
  }
}

void PeerConnection::handle_connect(const asio::error_code& err,
                                    tcp::resolver::iterator endpoint_iterator) {
  m_logger->debug(PRETTY_FUNCTION);
  if (!err) {
    // The connection was successful. Send the request.
    asio::async_write(socket_, request_,
                      bind(&PeerConnection::handle_response, this, _1));
  } else if (endpoint_iterator != tcp::resolver::iterator()) {
    // The connection failed. Try the next endpoint in the list.
    socket_.close();
    tcp::endpoint endpoint = *endpoint_iterator;
    // FIXME: Should set this after connection ok instead
    endpoint_ = endpoint_iterator;
    socket_.async_connect(endpoint, bind(&PeerConnection::handle_connect, this,
                                         _1, ++endpoint_iterator));
  } else {
    endpoint_ = {};
    m_logger->error(err.message());
  }
}

void PeerConnection::handle_response(const asio::error_code& err) {
  m_logger->debug(PRETTY_FUNCTION);
  if (!err) {
    if (response_.size()) {
      bytes response(response_.size());
      buffer_copy(asio::buffer(response), response_.data());
      Message msg(response);
      auto done = msg.parse(*this);
      cout.flush();
      // If we are done reading the current message we should return
      // but if not we should read more from the socket.
      if (done) {
        response_.consume(response_.size());
        return;
      }
    }

    // Read remaining data until EOF.
    // TODO: https://sourceforge.net/p/asio/mailman/message/23968189/
    //       mentions that maybe socket.async_read_some is better to read > 512
    //       bytes at a time
    asio::async_read(socket_, response_, asio::transfer_at_least(1),
                     bind(&PeerConnection::handle_response, this, _1));
  } else if (err != asio::error::eof) {
    m_logger->error(err.message());
  } else {
    m_logger->info("handle_response EOF");
  }
}

void Peer::set_am_choking(bool am_choking) {
  m_am_choking = am_choking;
}

void Peer::set_am_interested(bool am_interested) {
  // TODO: Extract message sending part
  if (!m_am_interested && am_interested) {
    // Send INTERESTED
    string interested = {0, 0, 0, 1,
                         static_cast<pwid_t>(peer_wire_id::INTERESTED)};
    stringstream hs;
    hs.write(interested.c_str(),
             numeric_cast<std::streamsize>(interested.length()));
    m_connection->write(hs.str());
  }

  if (m_am_interested && !am_interested) {
    // Send NOT_INTERESTED
    string interested = {0, 0, 0, 1,
                         static_cast<pwid_t>(peer_wire_id::NOT_INTERESTED)};
    stringstream hs;
    hs.write(interested.c_str(),
             numeric_cast<std::streamsize>(interested.length()));
    m_connection->write(hs.str());
  }

  m_am_interested = am_interested;
}

void Peer::request_next_block() {
  // We can now start requesting pieces
  auto has_piece = next_piece();
  if (!has_piece) {
    m_logger->info("No pieces left, nothing to do!");
    return;
  }
  auto piece = *has_piece;

  auto len = to_big_endian(13);
  auto index = to_big_endian(piece->id());
  auto block_offset = piece->next_offset();
  if (!block_offset) {
    m_logger->info("No block requests left to do!");
    return;
  }
  m_logger->info("Sending piece request");
  auto begin = to_big_endian(*block_offset);
  // 16 KiB (as recommended)
  auto length = to_big_endian(piece->block_size());
  bytes request;
  request.insert(request.end(), len.begin(), len.end());
  request.push_back(static_cast<byte>(peer_wire_id::REQUEST));
  request.insert(request.end(), index.begin(), index.end());
  request.insert(request.end(), begin.begin(), begin.end());
  request.insert(request.end(), length.begin(), length.end());
  m_connection->write(request);
}

void Peer::set_choking(bool choking) {
  if (m_choking && !choking) {  // Unchoked
    m_logger->info("Unchoked");
    request_next_block();
  }

  m_choking = choking;
}

void Peer::set_interested(bool interested) {
  m_interested = interested;
}

void Peer::set_remote_pieces(Bitfield bf) {
  m_remote_pieces = move(bf);
  if (!m_client_pieces.size()) {
    m_client_pieces = Bitfield(m_remote_pieces.size());
  }
}

void Peer::set_block(uint32_t piece_id, uint32_t offset, const bytes& data) {
  // Look up relevant piece object among active pieces
  if (m_active_pieces.find(piece_id) != m_active_pieces.end()) {
    auto piece = m_active_pieces[piece_id];
    if (piece->set_block(offset, data)) {
      m_logger->info("Piece {} done!", piece_id);
      m_client_pieces[piece_id] = true;
    }
    request_next_block();
  } else {
    m_logger->warn("Tried to set block for non active piece");
  }
}

void Peer::handshake(const Sha1& info_hash) {
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
  const string RESERVED = {0, 0, 0, 0, 0, 0, 0, 0};

  stringstream hs;
  hs << static_cast<char>(19) << "BitTorrent protocol";
  hs.write(RESERVED.c_str(), numeric_cast<std::streamsize>(RESERVED.length()));
  hs << info_hash.str() << "abcdefghijklmnopqrst";  // FIXME: Use proper peer-id

  auto port = 20000;  // FIXME: configurable port

  // Assume we need to start listening immediately, then send handshake
  asio::io_service io_service;
  try {
    m_connection = make_unique<PeerConnection>(*this, io_service, port);
  } catch (const asio::system_error&) {
    throw_with_nested(runtime_error("Creating peer connection to " +
                                    m_url.authority() + " from port " +
                                    to_string(port)));
  }
  m_connection->write(m_url, hs.str());
  io_service.run();

  // First test ...
}

optional<shared_ptr<Piece>> Peer::next_piece() {
  // Pieces the remote has minus the pieces we already got
  Bitfield relevant_pieces = m_remote_pieces - m_client_pieces;

  // Is there a piece to get
  auto next_id = relevant_pieces.next(true);
  if (!next_id) {
    return {};
  }

  // Is the piece active or not
  auto id = numeric_cast<uint32_t>(*next_id);
  auto piece = m_active_pieces.find(id);
  if (piece == m_active_pieces.end()) {
    auto it = m_active_pieces.emplace(
        make_pair(id, make_shared<Piece>(id, m_piece_length)));
    return make_optional(it.first->second);
  }
  return make_optional(m_active_pieces.at(id));
}

}  // namespace zit
