// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "torrent.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <tuple>

namespace zit {

using TorrentPiece = std::tuple<Torrent*, std::shared_ptr<Piece>>;

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
  FileWriter() : m_logger(spdlog::get("console")) {}

  /**
   * Add a piece to the queue for writing by the writer thread.
   */
  void add(Torrent* torrent, const std::shared_ptr<Piece>& piece);

  /**
   * Start FileWriter. It will pick pieces from the queue and write to disk as
   * they arrive.
   */
  void run();

  /**
   * Call this to stop the FileWriter. If will stop after finishing current
   * write.
   */
  void stop() {
    m_stop = true;
    m_condition.notify_one();
  }

 private:
  void write_next_piece();

  std::queue<TorrentPiece> m_queue{};
  std::mutex m_mutex{};
  std::condition_variable m_condition{};
  std::shared_ptr<spdlog::logger> m_logger{};
  std::atomic_bool m_stop = false;
};

class FileWriterThread {
 public:
  FileWriterThread()
      : m_logger(spdlog::get("console")),
        m_file_writer_thread([this]() { m_file_writer.run(); }) {}
  ~FileWriterThread() {
    m_logger->debug("FileWriter stopping");
    m_file_writer.stop();
    m_file_writer_thread.join();
  }

  FileWriter& get() { return m_file_writer; }

 private:
  std::shared_ptr<spdlog::logger> m_logger{};
  FileWriter m_file_writer{};
  std::thread m_file_writer_thread;
};

}  // namespace zit
