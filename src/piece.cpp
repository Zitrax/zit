// -*- mode:c++; c-basic-offset : 2; -*-
#include "piece.h"

#include "spdlog/spdlog.h"

#include "file_writer.h"

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

bytes Piece::get_block(uint32_t offset,
                       const filesystem::path& filename) const {
  if (offset % m_block_size != 0) {
    throw runtime_error("Invalid block offset: " + to_string(offset));
  }
  if (offset >= m_piece_size) {
    throw runtime_error("Too large block offset: " + to_string(offset));
  }
  auto block_id = offset / m_block_size;

  lock_guard<mutex> lock(m_mutex);
  if (!m_blocks_done[block_id]) {
    m_logger->warn("Block {} in piece {} not done", block_id, m_id);
    return {};
  }
  // Is it in memory?
  if (!m_piece_written) {
    m_logger->debug("Returning block {} in piece {} from memory", block_id,
                    m_id);
    auto start = m_data.begin() + offset;
    return bytes(start, start + m_block_size);
  }
  // Is it on disk?
  m_logger->debug("Returning block {} in piece {} from disk", block_id, m_id);
  return FileWriter::getInstance().read_block(offset, m_block_size, filename);
}

void Piece::set_piece_written(bool written) {
  m_piece_written = written;
  lock_guard<mutex> lock(m_mutex);
  for (uint32_t i = 0; i < block_count(); ++i) {
    m_blocks_done[i] = true;
  }
  m_data.clear();
  m_data.shrink_to_fit();
}

}  // namespace zit
