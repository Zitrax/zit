// -*- mode:c++; c-basic-offset : 2; -*-
#include "piece.h"

using namespace std;

namespace zit {

optional<uint32_t> Piece::next_offset() {
  auto next = m_blocks_requested.next(false);
  if (!next) {
    return {};
  }
  // Mark block as requested
  m_blocks_requested[*next] = true;
  return *next * m_block_size;
}

void Piece::set_block(uint32_t offset, const bytes& data) {
  if (offset % m_block_size != 0) {
    throw runtime_error("Invalid block offset: " + to_string(offset));
  }
  if (data.size() > m_block_size) {
    throw runtime_error("Block too big: " + to_string(data.size()));
  }
  auto block_id = offset / m_block_size;
  if (!m_blocks_requested[block_id]) {
    cerr << "WARNING: Got data for non requested block?\n";
  }
  copy(data.cbegin(), data.cend(), m_data.begin() + offset);
  m_blocks_done[block_id] = true;
  cout << "Block " << block_id << "/" << m_piece_size / m_block_size
       << " of size " << data.size() << " stored.\n";
}

}  // namespace zit