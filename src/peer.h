// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "net.h"
#include "sha1.h"

namespace zit {

class Peer {
 public:
  explicit Peer(Url url) : m_url(std::move(url)) {}

  auto url() const { return m_url; }
  auto am_choking() const { return m_am_choking; }
  auto am_interested() const { return m_am_interested; }
  auto choking() const { return m_choking; }
  auto interested() const { return m_interested; }

  void handshake(const sha1& info_hash);

 private:
  Url m_url;
  bool m_am_choking = true;
  bool m_am_interested = false;
  bool m_choking = true;
  bool m_interested = false;
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
