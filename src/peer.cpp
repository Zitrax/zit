// -*- mode:c++; c-basic-offset : 2; -*-
#include "peer.h"

namespace zit {

void Peer::handshake(const std::string& /*info_hash*/) {
  // The handshake should contain:
  // <pstrlen><pstr><reserved><info_hash><peer_id>
}

}  // namespace zit
