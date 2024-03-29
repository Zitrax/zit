// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "bitfield.hpp"
#include "net.hpp"
#include "piece.hpp"
#include "sha1.hpp"
#include "types.hpp"

#include "spdlog/spdlog.h"

#include <asio.hpp>

#include <deque>
#include <map>
#include <memory>
#include <optional>

namespace zit {

class Peer;
class Torrent;

/**
 * Each Peer uses a PeerConnection to handle the network connection and
 * traffic.
 */
class PeerConnection {
 public:
  PeerConnection(Peer& peer,
                 asio::io_service& io_service,
                 ListeningPort listening_port,
                 ConnectionPort connection_port);

  void write(const std::optional<Url>& url, const bytes& msg);
  void write(const std::optional<Url>& url, const std::string& msg);
  void write(const std::string& msg);
  void write(const bytes& msg);
  [[nodiscard]] Peer& peer() { return peer_; }
  [[nodiscard]] bool connected() const { return m_connected; }

  /**
   * Put the connection in listen mode to accept incoming connections.
   */
  void listen();

  /**
   * Stops the peer connection.
   */
  void stop();

 private:
  void handle_resolve(const asio::error_code& err,
                      asio::ip::tcp::resolver::iterator endpoint_iterator);

  void handle_connect(const asio::error_code& err,
                      asio::ip::tcp::resolver::iterator endpoint_iterator);

  void handle_response(const asio::error_code& err,
                       std::size_t bytes_transferred);

  void send(bool start_read = false);

  std::string m_msg{};
  Peer& peer_;
  asio::ip::tcp::resolver resolver_;
  asio::ip::tcp::acceptor acceptor_;
  asio::streambuf response_{};
  asio::ip::tcp::socket socket_;
  asio::ip::tcp::resolver::iterator endpoint_{};
  std::deque<std::string> m_send_queue{};
  bool m_connected = false;
  bool m_sending = false;
  ListeningPort m_listening_port;
  ConnectionPort m_connection_port;
};

/**
 * A peer that the torrent can connect to either to send to or download from.
 */
class Peer {
 public:
  explicit Peer(Url url, Torrent& torrent)
      : m_url(std::move(url)),
        m_torrent(torrent),
        m_last_activity(std::chrono::system_clock::now()) {}

  /**
   * A listening host does not need a url when created.
   */
  explicit Peer(Torrent& torrent) : m_torrent(torrent) {}

  [[nodiscard]] auto url() const { return m_url; }

  /**
   * This client is choking the remote peer.
   */
  [[nodiscard]] auto am_choking() const { return m_am_choking; }

  /**
   * This client is interested in the remote peer.
   */
  [[nodiscard]] auto am_interested() const { return m_am_interested; }

  /**
   * Remote peer is choking this client.
   */
  [[nodiscard]] auto choking() const { return m_choking; }

  /**
   * Remote peer is interested in this client.
   */
  [[nodiscard]] auto interested() const { return m_interested; }

  /**
   * Peer request a block in a piece from us.
   */
  void request(uint32_t index, uint32_t begin, uint32_t length);

  /**
   * String representation of the peer.
   */
  [[nodiscard]] auto str() const {
    return m_url ? m_url->authority() : "<no url>";
  }

  /**
   * Verify that an info hash is the one we are handling.
   */
  [[nodiscard]] bool verify_info_hash(const Sha1& info_hash) const;

  /**
   * Report the pieces we have to the remote client. This will only be done if
   * we have any pieces.
   */
  void report_bitfield() const;

  /**
   * Initiate a handshake with the remote peer. After this Torrent::run() need
   * to run to handle the network events.
   */
  void handshake();

  /**
   * Alternative to handshake() to listen to incoming connections.
   */
  void listen();

  /**
   * Return true if this peer is listening to incoming connections.
   */
  [[nodiscard]] bool is_listening() const { return m_listening; }

  /**
   * Return next piece.
   *
   * @param non_requested if true the piece returned must have at least 1 non
   * requested block.
   */
  [[nodiscard]] std::optional<std::shared_ptr<Piece>> next_piece(
      bool non_requested = false);

  void set_am_choking(bool am_choking);
  void set_am_interested(bool am_interested);
  void set_choking(bool choking);
  void set_interested(bool interested);

  void set_remote_pieces(Bitfield bf);

  /**
   * Remote peer reporting having piece with specific id
   */
  void have(uint32_t id);

  /**
   * Store a retrieved a block ( part of a piece )
   */
  void set_block(uint32_t piece_id, uint32_t offset, bytes_span data);

  /**
   * Stop this connection.
   */
  void stop();

  /**
   * The torrent associated with this peer.
   */
  [[nodiscard]] Torrent& torrent() { return m_torrent; }

  [[nodiscard]] asio::io_service& io_service() {
    if (!m_io_service) {
      throw std::invalid_argument("null io_service");
    }
    return *m_io_service;
  }

  std::size_t request_next_block(unsigned short count = 5);

  /**
   * Update the last activity time stamp to be now.
   */
  void update_activity() { m_last_activity = std::chrono::system_clock::now(); }

  /**
   * Return true if this peer is inactive.
   */
  [[nodiscard]] bool is_inactive() const;

 private:
  std::optional<Url> m_url{};
  bool m_am_choking = true;
  bool m_am_interested = false;
  bool m_choking = true;
  bool m_interested = false;
  bool m_listening = false;

  void init_io_service();

  Bitfield m_remote_pieces{};

  // order important - connection need to be destroyed first
  std::unique_ptr<asio::io_service> m_io_service{};
  std::unique_ptr<PeerConnection> m_connection{};

  Torrent& m_torrent;
  std::unique_ptr<asio::io_service::work> m_work{};
  std::chrono::system_clock::time_point m_last_activity{};
};

inline std::ostream& operator<<(std::ostream& os, const zit::Peer& peer) {
  os << "Am choking:    " << peer.am_choking() << "\n"
     << "Am interested: " << peer.am_interested() << "\n"
     << "Choking:       " << peer.choking() << "\n"
     << "Interested:    " << peer.interested() << "\n"
     << peer.str();
  return os;
}

}  // namespace zit
