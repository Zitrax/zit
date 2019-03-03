// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "torrent.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <tuple>

namespace zit {

using TorrentPiece =
    std::tuple<std::shared_ptr<Torrent>, std::shared_ptr<Piece>>;

/**
 * Queue of pieces to write to disk.
 * Whenever a piece is done it will be added to the queue,
 * when the FileWriter has written the piece to disk the piece data will
 * be freed from ram.
 *
 * Multi-file torrents will have pieces mapping to the files in order listed.
 * It's a continuous stream which means that in the boundaries one piece may
 * map to more than one file.
 */
class FileWriter {
 public:
  /**
   * Add a pice to the queue for writing by the writer thread.
   */
  void add(std::shared_ptr<Torrent> torrent, std::shared_ptr<Piece> piece);

 private:
  void write_next_piece();

  std::queue<TorrentPiece> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_condition;
};

}  // namespace zit
