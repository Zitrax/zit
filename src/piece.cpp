// -*- mode:c++; c-basic-offset : 2; -*-
#include "piece.h"

#include "spdlog/spdlog.h"

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

bool Piece::set_block(uint32_t offset, const bytes& data) {
  if (offset % m_block_size != 0) {
    throw runtime_error("Invalid block offset: " + to_string(offset));
  }
  if (data.size() > m_block_size) {
    throw runtime_error("Block too big: " + to_string(data.size()));
  }
  auto block_id = offset / m_block_size;
  if (m_blocks_done[block_id]) {
    m_logger->warn("Already got this block");
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

}  // namespace zit
