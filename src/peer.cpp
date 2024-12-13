// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.hpp"

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/completion_condition.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/io_service.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/socket_base.hpp>
#include <asio/system_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#ifndef _MSC_VER
#include <bits/basic_string.h>
#endif  // !_MSC_VER
#if __clang__
#include <bits/chrono.h>
#endif  // __clang__
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "bitfield.hpp"
#include "logger.hpp"
#include "messages.hpp"
#include "net.hpp"
#include "piece.hpp"
#include "sha1.hpp"
#include "string_utils.hpp"
#include "torrent.hpp"
#include "types.hpp"

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

PeerConnection::PeerConnection(IConnectionUrlProvider& peer,
                               asio::io_service& io_service,
                               ConnectionPort connection_port,
                               socket_ptr socket)
    : peer_(peer),
      resolver_(io_service),
      socket_(socket ? std::move(socket)
                     : make_unique<tcp::socket>(io_service, tcp::v4())),
      m_connection_port(connection_port),
      m_io_service(io_service) {
  // Note use of socket constructor that does not bind such
  // that we can set the options before that.
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
  logger()->trace(PRETTY_FUNCTION);

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

Peer& PeerConnection::peer() {
  // In production this is always valid - but during unit testing it might not
  // be a peer object but rather a mock.
  return dynamic_cast<Peer&>(peer_);
}

void PeerConnection::handle_resolve(const asio::error_code& err,
                                    tcp::resolver::iterator endpoint_iterator) {
  logger()->trace(PRETTY_FUNCTION);
  if (!err) {
    // Attempt a connection to the first endpoint in the list. Each endpoint
    // will be tried until we successfully establish a connection.
    const tcp::endpoint endpoint = *endpoint_iterator;
    // FIXME: Should set this after connection ok instead
    endpoint_ = endpoint_iterator;
    const asio::socket_base::reuse_address option(true);
    socket_->set_option(option);
    socket_->bind(tcp::endpoint(tcp::v4(), m_connection_port.get()));
    socket_->async_connect(endpoint,
                           [this, it = ++endpoint_iterator](auto&& ec) {
                             handle_connect(ec, it);
                           });
  } else {
    logger()->error("Resolve failed for {}: {}", peer().str(), err.message());
  }
}

void PeerConnection::send(bool start_read) {
  if (m_msg.empty()) {
    throw runtime_error("Send called with empty message");
  }
  if (!m_sending) {
    m_sending = true;
    const string msg(m_msg);
    m_msg.clear();
    asio::async_write(
        *socket_, asio::buffer(msg.c_str(), msg.size()),
        [this, start_read](auto err, auto len) {
          if (!err) {
            logger()->debug("{}: Data of len {} sent", peer_.str(), len);
          } else {
            logger()->error("{}: Write failed: {}", peer_.str(), err.message());
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
    logger()->debug("{}: Queued message of size {}", peer_.str(), m_msg.size());
    m_send_queue.push_back(m_msg);
    m_msg.clear();
  }
}

void PeerConnection::handle_connect(const asio::error_code& err,
                                    tcp::resolver::iterator endpoint_iterator) {
  logger()->trace(PRETTY_FUNCTION);
  if (!err) {
    // The connection was successful. Send the request.
    if (!m_connected) {
      m_connected = true;
      logger()->debug("Connected");
      send(true);
    } else {
      logger()->trace("Already connected");
      send();
    }
  } else if (endpoint_iterator != tcp::resolver::iterator()) {
    logger()->debug("Trying next endpoint");
    // The connection failed. Try the next endpoint in the list.
    socket_->close();
    const tcp::endpoint endpoint = *endpoint_iterator;
    // FIXME: Should set this after connection ok instead
    endpoint_ = endpoint_iterator;
    socket_->async_connect(endpoint,
                           [this, it = ++endpoint_iterator](const auto& ec) {
                             handle_connect(ec, it);
                           });
  } else {
    endpoint_ = {};
    // No need to spam about this. Quite normal.
    logger()->debug("Connect to {} failed: {}", peer_.str(), err.message());
  }
}

void PeerConnection::handle_response(const asio::error_code& err, std::size_t) {
  logger()->trace(PRETTY_FUNCTION);
  if (!err) {
    // Loop over buffer since we might have zero, one or more
    // messages waiting.
    bool done = false;
    while (!done && response_.size()) {
      bytes response(response_.size());
      buffer_copy(asio::buffer(response), response_.data());
      Message msg(response);
      const size_t consumed = msg.parse(*this);
      cout.flush();
      logger()->debug("Consuming {}/{}", consumed, response.size());
      response_.consume(consumed);
      done = consumed == 0;
    }

    // Read remaining data until EOF.
    // TODO: https://sourceforge.net/p/asio/mailman/message/23968189/
    //       mentions that maybe socket.async_read_some is better to read > 512
    //       bytes at a time
    asio::async_read(
        *socket_, response_, asio::transfer_at_least(1),
        [this](const auto& ec, auto s) { handle_response(ec, s); });
  } else if (err != asio::error::eof) {
    logger()->error("Response failed: {}", err.message());
  } else {
    logger()->debug("handle_response EOF");
    peer_.disconnected();
  }
}

void PeerConnection::stop() {
  resolver_.cancel();
  socket_->close();
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::map<ListeningPort, PeerAcceptor> PeerAcceptor::m_acceptors;

PeerAcceptor::PeerAcceptor(ListeningPort port,
                           asio::io_context& io_context,
                           std::string bind_address)
    : m_port(port),
      m_io_context(io_context),
      m_bind_address(std::move(bind_address)) {
  logger()->trace(PRETTY_FUNCTION);
  // For now rethrowing - can consider asio::detached later?
  co_spawn(m_io_context, listen(), [](auto e) { std::rethrow_exception(e); });
}

asio::awaitable<void> PeerAcceptor::listen() {
  logger()->trace(PRETTY_FUNCTION);
  asio::ip::tcp::acceptor acceptor{m_io_context, tcp::v4()};
  const asio::socket_base::reuse_address option(true);
  acceptor.set_option(option);
  if (m_bind_address.empty()) {
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), m_port.get()));
  } else {
    acceptor.bind(asio::ip::tcp::endpoint(
        asio::ip::make_address(m_bind_address), m_port.get()));
  }
  acceptor.listen();

  logger()->info("Listening for incoming connections on {}:{}",
                 acceptor.local_endpoint().address().to_string(),
                 acceptor.local_endpoint().port());

  while (true) {
    try {
      auto socket = co_await acceptor.async_accept(asio::use_awaitable);
      auto ip = socket.remote_endpoint().address().to_string();
      auto port = socket.remote_endpoint().port();
      logger()->info("Accepted new connection from {}:{}", ip, port);

      // Read data from socket
      bytes buffer;
      co_await asio::async_read(socket, asio::dynamic_buffer(buffer),
                                asio::transfer_at_least(MIN_BT_MSG_LENGTH),
                                asio::use_awaitable);
      auto handshake = HandshakeMsg::parse(buffer);
      if (!handshake) {
        logger()->warn("PeerAcceptor: Invalid handshake");
        continue;
      }
      Torrent* torrent = Torrent::get(handshake->getInfoHash());
      if (!torrent) {
        logger()->warn("PeerAcceptor: Unknown info_hash {}",
                       handshake->getInfoHash());
        continue;
      }
      // Now add a new listening peer for this torrent
      const auto accept_port = [&]() -> decltype(port) {
        // FIXME: Remove hardcoded ip
        if (ip == "192.168.0.18") {
          logger()->info("Translating port for Docker testing");
          return 51413;
        } else {
          return port;
        }
      }();
      torrent->add_peer(make_shared<Peer>(
          Url{fmt::format("http://{}:{}", ip, accept_port)}, *torrent));
    } catch (const std::system_error& error) {
      logger()->error("Listen errored: {}", error.what());
    }
  }
}

void Peer::request(uint32_t index, uint32_t begin, uint32_t length) {
  logger()->trace("Peer::request(index={}, begin={}, length={})", index, begin,
                  length);
  if (m_am_choking) {
    logger()->debug("{}: Choking peer, not sending blocks", str());
    return;
  }
  if (!m_interested) {
    logger()->debug("{}: Peer not interested, not sending blocks", str());
    return;
  }
  auto piece = m_torrent.active_piece(index);
  if (!piece) {
    logger()->warn("Requested non existing piece {}", index);
    return;
  }
  auto data = piece->get_block(begin, m_torrent, length);
  if (data.empty()) {
    logger()->warn("Empty block data - request failed");
    return;
  }
  logger()->debug("Sending PIECE {}", piece->id());
  bytes msg;
  auto len = to_big_endian<uint32_t>(numeric_cast<uint32_t>(9 + data.size()));
  auto offset = to_big_endian<uint32_t>(begin);
  auto idx = to_big_endian<uint32_t>(index);
  msg.insert(msg.cend(), len.begin(), len.end());
  msg.push_back(static_cast<byte>(peer_wire_id::PIECE));
  msg.insert(msg.cend(), idx.begin(), idx.end());
  msg.insert(msg.cend(), offset.begin(), offset.end());
  msg.insert(msg.cend(), data.begin(), data.end());
  m_connection->write(msg);
}

void Peer::set_am_choking(bool am_choking) {
  if (!m_am_choking && am_choking) {
    // Send UNCHOKE
    logger()->debug("Sending CHOKE");
    const string choke = {0, 0, 0, 1, static_cast<pwid_t>(peer_wire_id::CHOKE)};
    stringstream hs;
    hs.write(choke.c_str(), numeric_cast<std::streamsize>(choke.length()));
    m_connection->write(hs.str());
  }

  if (m_am_choking && !am_choking) {
    // Send UNCHOKE
    logger()->debug("Sending UNCHOKE");
    const string unchoke = {0, 0, 0, 1,
                            static_cast<pwid_t>(peer_wire_id::UNCHOKE)};
    stringstream hs;
    hs.write(unchoke.c_str(), numeric_cast<std::streamsize>(unchoke.length()));
    m_connection->write(hs.str());
  }

  m_am_choking = am_choking;
}

void Peer::set_am_interested(bool am_interested) {
  // TODO: Extract message sending part
  if (!m_am_interested && am_interested) {
    if (m_torrent.done()) {
      return;
    }
    // Send INTERESTED
    logger()->debug("Sending INTERESTED");
    const string interested_msg = {
        0, 0, 0, 1, static_cast<pwid_t>(peer_wire_id::INTERESTED)};
    stringstream hs;
    hs.write(interested_msg.c_str(),
             numeric_cast<std::streamsize>(interested_msg.length()));
    m_connection->write(hs.str());
  }

  if (m_am_interested && !am_interested) {
    // Send NOT_INTERESTED
    logger()->debug("Sending NOT_INTERESTED");
    const string interested_msg = {
        0, 0, 0, 1, static_cast<pwid_t>(peer_wire_id::NOT_INTERESTED)};
    stringstream hs;
    hs.write(interested_msg.c_str(),
             numeric_cast<std::streamsize>(interested_msg.length()));
    m_connection->write(hs.str());
  }

  m_am_interested = am_interested;
}

std::size_t Peer::request_next_block(unsigned short count) {
  size_t requests = 0;
  if (!m_am_interested) {
    logger()->debug(
        "{}: Peer not interested (no handshake), not requesting blocks", str());
    return requests;
  }
  if (m_choking) {
    logger()->debug("{}: Peer choked, not requesting blocks", str());
    return requests;
  }
  bytes req;
  for (int i = 0; i < count; i++) {
    // We can now start requesting pieces
    auto has_piece = next_piece(true);
    if (!has_piece) {
      logger()->debug("{}: No pieces left, nothing to do!", str());
      break;
    }
    auto piece = *has_piece;

    auto block_offset = piece->next_offset(true);
    if (!block_offset) {
      logger()->debug("{}: No block requests left to do!", str());
      break;
    }
    auto len = to_big_endian<uint32_t>(13);
    auto index = to_big_endian<uint32_t>(piece->id());
    auto begin = to_big_endian<uint32_t>(*block_offset);
    const auto LENGTH = [&] {
      if (*block_offset + piece->block_size() > piece->piece_size()) {
        // Last block - so reduce request size
        return piece->piece_size() % piece->block_size();
      }
      // 16 KiB (as recommended)
      return piece->block_size();
    }();
    logger()->debug(
        "{}: Sending block request for piece {} with size {} and offset {}",
        str(), piece->id(), LENGTH, *block_offset);
    auto blength = to_big_endian<uint32_t>(LENGTH);
    req.insert(req.end(), len.begin(), len.end());
    req.push_back(static_cast<byte>(peer_wire_id::REQUEST));
    req.insert(req.end(), index.begin(), index.end());
    req.insert(req.end(), begin.begin(), begin.end());
    req.insert(req.end(), blength.begin(), blength.end());
    requests++;
  }
  // Important to write only once (to match write/read calls)
  if (!req.empty()) {
    m_connection->write(req);
  }
  return requests;
}

bool Peer::is_inactive() const {
  const auto time_since_last_activity =
      std::chrono::system_clock::now() - m_last_activity;
  logger()->trace(
      "Time since last activity: {}s",
      duration_cast<chrono::seconds>(time_since_last_activity).count());
  // 2min suggested in torrent spec
  return time_since_last_activity >= 2min;
}

void Peer::set_choking(bool choking) {
  if (m_choking && !choking) {
    m_choking = choking;
    logger()->info("{}: Unchoked", str());
    request_next_block();
  }

  if (!m_choking && choking) {
    m_choking = choking;
    logger()->info("{}: Choked", str());
  }
}

void Peer::set_interested(bool interested) {
  if (!m_interested && interested) {
    logger()->info("Peer is Interested - sending unchoke");
    set_am_choking(false);
  }
  if (m_interested && !interested) {
    logger()->info("Peer is Not interested");
    // TODO: I guess we don't need to choke it since the peer told us
  }
  m_interested = interested;
}

void Peer::set_remote_pieces(Bitfield bf) {
  m_remote_pieces = std::move(bf);
  m_torrent.init_client_pieces(m_remote_pieces.size());
}

void Peer::have(uint32_t id) {
  if (m_remote_pieces.count() == 0) {
    // If we get a have message before a bitfield, we assume that the remote
    // has all pieces. This does not really seem to help, might remove or make
    // configurable later.
    logger()->warn("Remote never sent bitfield - assuming it has all pieces");
    const auto [pieces_downloaded, number_of_pieces] = m_torrent.piece_status();
    m_torrent.init_client_pieces(number_of_pieces);
    m_remote_pieces = Bitfield(number_of_pieces);
    m_remote_pieces.fill(number_of_pieces, true);
  } else {
    m_remote_pieces[id] = true;
  }
  request_next_block();
}

void Peer::set_block(uint32_t piece_id, uint32_t offset, bytes_span data) {
  if (m_torrent.set_block(piece_id, offset, data)) {
    request_next_block(1);
  }
}

void Peer::stop() {
  logger()->info("Stopping peer {}", str());
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
    auto len =
        to_big_endian<uint32_t>(numeric_cast<uint32_t>(1 + bf.size_bytes()));
    msg.insert(msg.cend(), len.begin(), len.end());
    msg.push_back(static_cast<byte>(peer_wire_id::BITFIELD));
    msg.insert(msg.cend(), bf.data().begin(), bf.data().end());
    logger()->debug("Sending bitfield of size {}", msg.size());
    m_connection->write(msg);
  } else {
    logger()->debug("Not sending bitfield - no pieces");
  }
}

void Peer::handshake() {
  logger()->info("Starting handshake with: {}", str());

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
  hs << m_torrent.info_hash().str() << m_torrent.peer_id();

  init_io_service();
  m_connection->write(m_url, hs.str());
}

void Peer::disconnected() {
  m_torrent.disconnected(this);
}

void Peer::init_io_service(socket_ptr socket) {
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
    m_connection = make_unique<PeerConnection>(
        *this, *m_io_service, m_torrent.connection_port(), std::move(socket));
  } catch (const asio::system_error& err) {
    throw_with_nested(
        runtime_error("Creating peer connection to " + str() + err.what()));
  }
}

optional<shared_ptr<Piece>> Peer::next_piece(bool non_requested) {
  // Pieces the remote has minus the pieces we already got
  const Bitfield relevant_pieces = m_torrent.relevant_pieces(m_remote_pieces);

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
      return piece;
    }
    (*next_id)++;
  }
}

}  // namespace zit
