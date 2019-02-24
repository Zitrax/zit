// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>
#include "bitfield.h"
#include "types.h"

namespace zit {

/**
 * Represents one torrent piece and its current state.
 */
class Piece {
 public:
  explicit Piece(uint32_t id, uint32_t piece_size)
      : m_piece_size(piece_size),
        m_blocks_requested(piece_size / m_block_size),
        m_blocks_done(piece_size / m_block_size),
        m_id(id) {
    m_data.reserve(m_piece_size);
  }

  /** Piece id */
  [[nodiscard]] auto id() const { return m_id; }
  /** Next offset to request */
  [[nodiscard]] std::optional<uint32_t> next_offset();
  /** Total block size in bytes */
  [[nodiscard]] auto block_size() const { return m_block_size; }
  /** Store incoming data */
  void set_block(uint32_t offset, const bytes& data);

 private:
  const uint32_t m_block_size = 1 << 14;
  uint32_t m_piece_size;
  Bitfield m_blocks_requested;
  Bitfield m_blocks_done;
  uint32_t m_id;
  bytes m_data{};
};

}  // namespace zit
