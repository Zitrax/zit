// -*- mode:c++; c-basic-offset : 2; -*-
#include "piece.h"

#include "spdlog/spdlog.h"

using namespace std;

namespace zit {

optional<uint32_t> Piece::next_offset() {
  lock_guard<mutex> lock(m_mutex);
  auto next = m_blocks_requested.next(false);
  if (!next) {
    return {};
  }
  // The current bitfield only stores whole bytes
  // but the last piece might contain fewer blocks
  // thus this extra check.
  if (*next > (block_count() - 1)) {
    return {};
  }
  // Mark block as requested
  m_blocks_requested[*next] = true;
  return *next * m_block_size;
}

bytes Piece::data() const {
  lock_guard<mutex> lock(m_mutex);
  if (m_data.empty()) {
    m_logger->warn("Retrieved empty data from piece {}", m_id);
  }
  return m_data;
}

bool Piece::set_block(uint32_t offset, const bytes& data) {
  if (offset % m_block_size != 0) {
    throw runtime_error("Invalid block offset: " + to_string(offset));
  }
  if (data.size() > m_block_size) {
    throw runtime_error("Block too big: " + to_string(data.size()));
  }
  if (data.size() + offset > m_piece_size) {
    throw runtime_error("Block overflows piece");
  }
  auto block_id = offset / m_block_size;

  lock_guard<mutex> lock(m_mutex);
  if (m_blocks_done[block_id]) {
    m_logger->warn("Already got block {} for piece {}", block_id, m_id);
  } else {
    if (!m_blocks_requested[block_id]) {
      m_logger->warn("Got data for non requested block?");
    }
    copy(data.cbegin(), data.cend(), m_data.begin() + offset);
    m_blocks_done[block_id] = true;
    m_logger->info("Block {}/{} of size {} stored for piece {}", block_id + 1,
                   block_count(), data.size(), m_id);
  }
  auto next = m_blocks_done.next(false);
  return !next || *next >= block_count();
}

void Piece::set_piece_written(bool written) {
  m_piece_written = written;
  lock_guard<mutex> lock(m_mutex);
  m_data.clear();
  m_data.shrink_to_fit();
}

}  // namespace zit
