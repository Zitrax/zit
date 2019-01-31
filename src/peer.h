// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "bitfield.h"
#include "net.h"
#include "sha1.h"
#include "types.h"

#include <asio.hpp>

#include <memory>
#include <optional>

namespace zit {

class Peer;

class PeerConnection {
 public:
  PeerConnection(Peer& peer,
                 asio::io_service& io_service,
                 unsigned short port_num);

  void write(const Url& url, const bytes& msg);
  void write(const Url& url, const std::string& msg);
  void write(const std::string& msg);
  void write(const bytes& msg);
  [[nodiscard]] Peer& peer() { return peer_; }

 private:
  void handle_resolve(const asio::error_code& err,
                      asio::ip::tcp::resolver::iterator endpoint_iterator);

  void handle_connect(const asio::error_code& err,
                      asio::ip::tcp::resolver::iterator endpoint_iterator);

  void handle_response(const asio::error_code& err);

  asio::streambuf request_{};
  Peer& peer_;
  asio::ip::tcp::resolver resolver_;
  asio::streambuf response_{};
  asio::ip::tcp::socket socket_;
  asio::ip::tcp::resolver::iterator endpoint_{};
};

class Peer {
 public:
  explicit Peer(Url url) : m_url(std::move(url)) {}

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

  void handshake(const Sha1& info_hash);

  void set_am_choking(bool am_choking);
  void set_am_interested(bool am_interested);
  void set_choking(bool choking);
  void set_interested(bool interested);

 private:
  Url m_url;
  bool m_am_choking = true;
  bool m_am_interested = false;
  bool m_choking = true;
  bool m_interested = false;
  Bitfield m_pieces{};
  std::unique_ptr<PeerConnection> m_connection{};
};

inline std::ostream& operator<<(std::ostream& os, const zit::Peer& url) {
  os << "Am choking:    " << url.am_choking() << "\n"
     << "Am interested: " << url.am_interested() << "\n"
     << "Choking:       " << url.choking() << "\n"
     << "Interested:    " << url.interested() << "\n"
     << url.url();
  return os;
}

}  // namespace zit
