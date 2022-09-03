// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.h"

#include "string_utils.h"

#include <iostream>
#include <memory>
#include <optional>
#include "messages.h"
#include "spdlog/spdlog.h"
#include "torrent.h"

using asio::ip::tcp;
using namespace std;

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
                               unsigned short listening_port,
                               unsigned short connection_port)
    : peer_(peer),
      resolver_(io_service),
      acceptor_(io_service, tcp::v4()),
      socket_(io_service, tcp::v4()),
      m_listening_port(listening_port),
      m_connection_port(connection_port),
      m_logger(spdlog::get("console")) {
  // Note use of socket constructor that does not bind such
  // that we can set the options before that.
}

void PeerConnection::listen() {
  m_logger->info("{} port={}", PRETTY_FUNCTION, m_listening_port);
  // if (!acceptor_.is_open()) {
  asio::socket_base::reuse_address option(true);
  acceptor_.set_option(option);
  acceptor_.bind(tcp::endpoint(tcp::v4(), m_listening_port));
  //}
  acceptor_.listen();
  acceptor_.async_accept(
      [this](const asio::error_code& error, asio::ip::tcp::socket new_socket) {
        if (!error) {
          auto ip = new_socket.remote_endpoint().address().to_string();
          auto port = new_socket.remote_endpoint().port();
          m_logger->warn("Accepted new connection from {}:{}", ip, port);
          socket_ = move(new_socket);
          m_connected = true;
          asio::async_read(
              socket_, response_, asio::transfer_at_least(1),
              [this](const auto& ec, auto s) { handle_response(ec, s); });
        } else {
          m_logger->error("Listen errored: {}", error.message());
          listen();
        }

        // FIXME: Call listen again to accept further connections?
      });
}

void PeerConnection::write(const optional<Url>& url, const bytes& msg) {
  write(url, from_bytes(msg));
}

void PeerConnection::write(const bytes& msg) {
  write(peer_.url(), msg);
}

void PeerConnection::write(const std::string& msg) {
  write(peer_.url(), msg);
}

void PeerConnection::write(const optional<Url>& url, const std::string& msg) {
  m_logger->debug(PRETTY_FUNCTION);

  if (!m_msg.empty()) {
    throw runtime_error("Message not empty");
  }
  m_msg = msg;

  if (m_connected || endpoint_ != tcp::resolver::iterator()) {
    // We have already resolved and connected
    handle_connect(asio::error_code(), endpoint_);
  } else {
    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    if (!url) {
      throw runtime_error("write called with empty url");
    }
    resolver_.async_resolve(
        url->host(), url->service(),
        [this](auto&& ec, auto&& it) { handle_resolve(ec, it); });
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
    asio::socket_base::reuse_address option(true);
    socket_.set_option(option);
    socket_.bind(tcp::endpoint(tcp::v4(), m_connection_port));
    socket_.async_connect(endpoint, [this, it = ++endpoint_iterator](
                                        auto&& ec) { handle_connect(ec, it); });
  } else {
    m_logger->error("Resolve failed: {}", err.message());
  }
}

void PeerConnection::send(bool start_read) {
  if (m_msg.empty()) {
    throw runtime_error("Send called with empty message");
  }
  if (!m_sending) {
    m_sending = true;
    string msg(m_msg);
    m_msg.clear();
    asio::async_write(
        socket_, asio::buffer(msg.c_str(), msg.size()),
        [this, start_read](auto err, auto len) {
          if (!err) {
            m_logger->debug("{}: Data of len {} sent", peer_.str(), len);
          } else {
            m_logger->error("{}: Write failed: {}", peer_.str(), err.message());
          }
          m_sending = false;
          if (start_read) {
            handle_response({}, 0);
          }
          if (!m_send_queue.empty()) {
            m_msg = m_send_queue.front();
            m_send_queue.pop_front();
            send();
          }
        });
  } else {
    m_logger->debug("{}: Queued message of size {}", peer_.str(), m_msg.size());
    m_send_queue.push_back(m_msg);
    m_msg.clear();
  }
}

void PeerConnection::handle_connect(const asio::error_code& err,
                                    tcp::resolver::iterator endpoint_iterator) {
  m_logger->debug(PRETTY_FUNCTION);
  if (!err) {
    // The connection was successful. Send the request.
    if (!m_connected) {
      m_connected = true;
      m_logger->debug("Connected");
      send(true);
    } else {
      m_logger->debug("Already connected");
      send();
    }
  } else if (endpoint_iterator != tcp::resolver::iterator()) {
    m_logger->debug("Trying next endpoint");
    // The connection failed. Try the next endpoint in the list.
    socket_.close();
    tcp::endpoint endpoint = *endpoint_iterator;
    // FIXME: Should set this after connection ok instead
    endpoint_ = endpoint_iterator;
    socket_.async_connect(endpoint,
                          [this, it = ++endpoint_iterator](const auto& ec) {
                            handle_connect(ec, it);
                          });
  } else {
    endpoint_ = {};
    // No need to spam about this. Quite normal.
    m_logger->debug("Connect failed: {}", err.message());
  }
}

void PeerConnection::handle_response(const asio::error_code& err, std::size_t) {
  m_logger->trace(PRETTY_FUNCTION);
  if (!err) {
    // Loop over buffer since we might have zero, one or more
    // messages waiting.
    bool done = false;
    while (!done && response_.size()) {
      bytes response(response_.size());
      buffer_copy(asio::buffer(response), response_.data());
      Message msg(response);
      size_t consumed = msg.parse(*this);
      cout.flush();
      m_logger->debug("Consuming {}/{}", consumed, response.size());
      response_.consume(consumed);
      done = consumed == 0;
    }

    // Read remaining data until EOF.
    // TODO: https://sourceforge.net/p/asio/mailman/message/23968189/
    //       mentions that maybe socket.async_read_some is better to read > 512
    //       bytes at a time
    asio::async_read(
        socket_, response_, asio::transfer_at_least(1),
        [this](const auto& ec, auto s) { handle_response(ec, s); });
  } else if (err != asio::error::eof) {
    m_logger->error("Response failed: {}", err.message());
  } else {
    m_logger->debug("handle_response EOF");
    peer_.torrent().disconnected(&peer_);
  }
}

void PeerConnection::stop() {
  resolver_.cancel();
  acceptor_.close();
  socket_.close();
}

void Peer::request(uint32_t index, uint32_t begin, uint32_t length) {
  m_logger->trace("Peer::request(index={}, begin={}, length={})", index, begin,
                  length);
  auto piece = m_torrent.active_piece(index);
  if (!piece) {
    m_logger->warn("Requested non existing piece {}", index);
    return;
  }
  auto data = piece->get_block(begin, m_torrent, length);
  if (data.empty()) {
    m_logger->warn("Empty block data - request failed");
    return;
  }
  m_logger->debug("Sending PIECE {}", piece->id());
  bytes msg;
  auto len = to_big_endian(numeric_cast<uint32_t>(9 + data.size()));
  auto offset = to_big_endian(begin);
  auto idx = to_big_endian(index);
  msg.insert(msg.cend(), len.begin(), len.end());
  msg.push_back(static_cast<byte>(peer_wire_id::PIECE));
  msg.insert(msg.cend(), idx.begin(), idx.end());
  msg.insert(msg.cend(), offset.begin(), offset.end());
  msg.insert(msg.cend(), data.begin(), data.end());
  m_connection->write(msg);
}

void Peer::set_am_choking(bool am_choking) {
  m_am_choking = am_choking;
}

void Peer::set_am_interested(bool am_interested) {
  // TODO: Extract message sending part
  if (!m_am_interested && am_interested) {
    if (m_torrent.done()) {
      return;
    }
    // Send INTERESTED
    m_logger->debug("Sending INTERESTED");
    string interested = {0, 0, 0, 1,
                         static_cast<pwid_t>(peer_wire_id::INTERESTED)};
    stringstream hs;
    hs.write(interested.c_str(),
             numeric_cast<std::streamsize>(interested.length()));
    m_connection->write(hs.str());
  }

  if (m_am_interested && !am_interested) {
    // Send NOT_INTERESTED
    m_logger->debug("Sending NOT_INTERESTED");
    string interested = {0, 0, 0, 1,
                         static_cast<pwid_t>(peer_wire_id::NOT_INTERESTED)};
    stringstream hs;
    hs.write(interested.c_str(),
             numeric_cast<std::streamsize>(interested.length()));
    m_connection->write(hs.str());
  }

  m_am_interested = am_interested;
}

std::size_t Peer::request_next_block(unsigned short count) {
  size_t requests = 0;
  if (!m_am_interested) {
    m_logger->trace(
        "{}: Peer not interested (no handshake), not requesting blocks", str());
    return requests;
  }
  if (m_choking) {
    m_logger->debug("{}: Peer choked, not requesting blocks", str());
    return requests;
  }
  bytes request;
  for (int i = 0; i < count; i++) {
    // We can now start requesting pieces
    auto has_piece = next_piece(true);
    if (!has_piece) {
      m_logger->debug("{}: No pieces left, nothing to do!", str());
      break;
    }
    auto piece = *has_piece;

    auto block_offset = piece->next_offset(true);
    if (!block_offset) {
      m_logger->debug("{}: No block requests left to do!", str());
      break;
    }
    auto len = to_big_endian(13);
    auto index = to_big_endian(piece->id());
    auto begin = to_big_endian(*block_offset);
    const auto LENGTH = [&] {
      if (*block_offset + piece->block_size() > piece->piece_size()) {
        // Last block - so reduce request size
        return piece->piece_size() % piece->block_size();
      }
      // 16 KiB (as recommended)
      return piece->block_size();
    }();
    m_logger->debug(
        "{}: Sending block request for piece {} with size {} and offset {}",
        str(), piece->id(), LENGTH, *block_offset);
    auto blength = to_big_endian(LENGTH);
    request.insert(request.end(), len.begin(), len.end());
    request.push_back(static_cast<byte>(peer_wire_id::REQUEST));
    request.insert(request.end(), index.begin(), index.end());
    request.insert(request.end(), begin.begin(), begin.end());
    request.insert(request.end(), blength.begin(), blength.end());
    requests++;
  }
  // Important to write only once (to match write/read calls)
  if (!request.empty()) {
    m_connection->write(request);
  }
  return requests;
}

void Peer::set_choking(bool choking) {
  if (m_choking && !choking) {
    m_choking = choking;
    m_logger->info("{}: Unchoked", str());
    request_next_block();
  }

  if (!m_choking && choking) {
    m_choking = choking;
    m_logger->info("{}: Choked", str());
  }
}

void Peer::set_interested(bool interested) {
  if (!m_interested && interested) {
    m_logger->info("Peer is Interested - sending unchoke");
    string unchoke = {0, 0, 0, 1, static_cast<pwid_t>(peer_wire_id::UNCHOKE)};
    stringstream hs;
    hs.write(unchoke.c_str(), numeric_cast<std::streamsize>(unchoke.length()));
    m_connection->write(hs.str());
  }
  if (m_interested && !interested) {
    m_logger->info("Peer is Not interested");
    // TODO: I guess we don't need to choke it since the peer told us
  }
  m_interested = interested;
}

void Peer::set_remote_pieces(Bitfield bf) {
  m_remote_pieces = move(bf);
  m_torrent.init_client_pieces(m_remote_pieces.size());
}

void Peer::have(uint32_t id) {
  m_remote_pieces[id] = true;
}

void Peer::set_block(uint32_t piece_id, uint32_t offset, const bytes& data) {
  if (m_torrent.set_block(piece_id, offset, data)) {
    request_next_block(1);
  }
}

void Peer::stop() {
  m_logger->info("Stopping peer {}", str());
  m_connection->stop();
  m_io_service->stop();
}

bool Peer::verify_info_hash(const Sha1& info_hash) const {
  return m_torrent.info_hash() == info_hash;
}

void Peer::report_bitfield() const {
  // Only send bitfield information if we have at least one piece
  auto bf = m_torrent.client_pieces();
  if (bf.next(true)) {
    // Send BITFIELD
    bytes msg;
    auto len = to_big_endian(numeric_cast<uint32_t>(1 + bf.size_bytes()));
    msg.insert(msg.cend(), len.begin(), len.end());
    msg.push_back(static_cast<byte>(peer_wire_id::BITFIELD));
    msg.insert(msg.cend(), bf.data().begin(), bf.data().end());
    m_logger->debug("Sending bitfield of size {}", msg.size());
    m_connection->write(msg);
  } else {
    m_logger->debug("Not sending bitfield - no pieces");
  }
}

void Peer::handshake() {
  m_logger->info("Starting handshake with: {}", str());

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
  hs << m_torrent.info_hash().str()
     << "abcdefghijklmnopqrst";  // FIXME: Use proper peer-id

  init_io_service();
  m_connection->write(m_url, hs.str());
}

void Peer::listen() {
  m_listening = true;
  init_io_service();
  m_connection->listen();
}

void Peer::init_io_service() {
  if (m_io_service) {
    return;
  }
  // Assume we need to start listening immediately, then send handshake
  m_io_service = make_unique<asio::io_service>();
  // The work object make sure that the service does not stop when running out
  // of events but stay running until stop() is called on the io_service for a
  // hard stop, or destroy the work object for a graceful shutdown by handling
  // the remaining events.
  m_work = make_unique<asio::io_service::work>(*m_io_service);
  try {
    m_connection = make_unique<PeerConnection>(*this, *m_io_service,
                                               m_torrent.listning_port(),
                                               m_torrent.connection_port());
  } catch (const asio::system_error& err) {
    throw_with_nested(
        runtime_error("Creating peer connection to " + str() + err.what()));
  }
}

optional<shared_ptr<Piece>> Peer::next_piece(bool non_requested) {
  // Pieces the remote has minus the pieces we already got
  Bitfield relevant_pieces = m_torrent.relevant_pieces(m_remote_pieces);

  // Is there a piece to get
  std::optional<std::size_t> next_id{0};
  while (true) {
    next_id = relevant_pieces.next(true, *next_id);
    if (!next_id) {
      return {};
    }

    const auto id = numeric_cast<uint32_t>(*next_id);
    auto piece = m_torrent.active_piece(id);
    if (!non_requested || (piece && piece->next_offset(false))) {
      return std::move(piece);
    }
    (*next_id)++;
  }
}

}  // namespace zit
