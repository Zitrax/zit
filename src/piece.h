// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <cstdint>
#include <filesystem>
#include "bitfield.h"
#include "types.h"

#include "spdlog/spdlog.h"

namespace zit {

/**
 * Represents one torrent piece and its current state.
 */
class Piece {
 public:
  explicit Piece(uint32_t id, uint32_t piece_size)
      : m_piece_size(piece_size),
        m_blocks_requested(block_count()),
        m_blocks_done(block_count()),
        m_id(id),
        m_logger(spdlog::get("console")) {
    m_data.resize(m_piece_size);
  }

  /** Piece id */
  [[nodiscard]] auto id() const { return m_id; }
  /** Next offset to request
   * @param mark if true marks the block as requested
   */
  [[nodiscard]] std::optional<uint32_t> next_offset(bool mark);
  /** Total block size in bytes */
  [[nodiscard]] auto block_size() const { return m_block_size; }
  /** Total number of blocks in this piece */
  [[nodiscard]] uint32_t block_count() const {
    return (m_piece_size / m_block_size) +
           (m_piece_size % m_block_size ? 1 : 0);
  }

  /** Return the data of the piece */
  [[nodiscard]] bytes data() const;

  /** Size of this piece in bytes */
  [[nodiscard]] auto piece_size() const { return m_piece_size; }

  /**
   * Store incoming data
   * @return true if this was the last remaining block for this piece
   */
  bool set_block(uint32_t offset, const bytes& data);

  /**
   * Return a specific block. Can return empty vector if
   * the block is not yet done.
   *
   * @param offset the piece offset for the block
   * @param filename the file to read data from if not in memory
   */
  [[nodiscard]] bytes get_block(uint32_t offset,
                                const std::filesystem::path& filename) const;

  /**
   * Update status whether the piece has been written to disk or not.
   */
  void set_piece_written(bool written);

  /**
   * Return whether this piece has been written to disk or not.
   */
  [[nodiscard]] bool piece_written() const { return m_piece_written; }

  /**
   * If this piece has been inactive for a while, mark all requested blocks as
   * non requested such that we can try them again.
   *
   * Return the number of blocks cleared that were requested but not done.
   */
  [[nodiscard]] std::size_t retry_blocks();

 private:
  const uint32_t m_block_size = 1 << 14;
  uint32_t m_piece_size;
  Bitfield m_blocks_requested;
  Bitfield m_blocks_done;
  const uint32_t m_id;
  mutable std::mutex m_mutex{};
  bytes m_data{};
  std::atomic_bool m_piece_written = false;
  std::shared_ptr<spdlog::logger> m_logger;
  std::chrono::system_clock::time_point m_last_request{};
  std::chrono::system_clock::time_point m_last_block{};
};

}  // namespace zit
