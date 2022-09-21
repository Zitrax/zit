// -*- mode:c++; c-basic-offset : 2; -*-
#include "piece.hpp"

#include "spdlog/spdlog.h"

#include "file_writer.hpp"

using namespace std;

namespace zit {

optional<uint32_t> Piece::next_offset(bool mark) {
  lock_guard<mutex> lock(m_mutex);
  const auto req_or_done = m_blocks_requested + m_blocks_done;
  auto next = req_or_done.next(false);
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
  if (mark) {
    m_blocks_requested[*next] = true;
    m_last_request = std::chrono::system_clock::now();
  }
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
    ranges::copy(data, m_data.begin() + offset);
    m_blocks_done[block_id] = true;
    m_logger->debug("Block {}/{} of size {} stored for piece {}", block_id + 1,
                    block_count(), data.size(), m_id);
  }
  m_last_block = std::chrono::system_clock::now();
  auto next = m_blocks_done.next(false);
  return !next || *next >= block_count();
}

bytes Piece::get_block(uint32_t offset,
                       const Torrent& torrent,
                       uint32_t length) const {
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
  length = length ? length : m_block_size;
  // Is it in memory?
  if (!m_piece_written) {
    m_logger->debug("Returning block {} in piece {} from memory", block_id,
                    m_id);
    auto start = m_data.begin() + offset;
    return {start, start + length};
  }
  // Is it on disk?
  m_logger->debug("Returning block {} in piece {} from disk", block_id, m_id);
  auto file_offset = m_piece_size * m_id + offset;
  return FileWriter::getInstance().read_block(file_offset, length, torrent);
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

std::size_t Piece::retry_blocks() {
  if (m_piece_written) {
    return 0;
  }
  const auto last_activity = std::max(m_last_block, m_last_request);
  if (last_activity == std::chrono::system_clock::time_point::min()) {
    // Nothing happened yet
    return 0;
  }
  const auto now = std::chrono::system_clock::now();
  const auto inactive = now - last_activity;
  // False positive
  // NOLINTNEXTLINE(hicpp-use-nullptr,modernize-use-nullptr)
  if (inactive > 30s && m_blocks_requested.next(true).has_value()) {
    m_logger->warn(
        "Piece {} inactive for {} seconds. Marking for retry.", m_id,
        std::chrono::duration_cast<std::chrono::seconds>(inactive).count());
    lock_guard<mutex> lock(m_mutex);
    const auto retry = (m_blocks_requested - m_blocks_done).count();

    // TODO: Should we really request the whole piece again?
    m_blocks_requested = Bitfield(block_count());
    return retry;
  }
  return 0;
}

}  // namespace zit
