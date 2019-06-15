// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "torrent.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <tuple>

#include "spdlog/sinks/stdout_color_sinks.h"

using namespace std::placeholders;

namespace zit {

using TorrentPiece = std::tuple<Torrent*, std::shared_ptr<Piece>>;
using TorrentWrittenCallback = std::function<void(Torrent&)>;

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
  explicit FileWriter(TorrentWrittenCallback cb)
      : m_logger(spdlog::get("file_writer")), m_torrent_written_callback(std::move(cb)) {}

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
  TorrentWrittenCallback m_torrent_written_callback{};
};

/**
 * Managing the thread running  FileWriter
 */
class FileWriterThread {
 private:
  static auto init_logger() {
    auto logger = spdlog::get("file_writer");
    if (!logger) {
      logger = spdlog::stdout_color_mt("file_writer");
    }
    logger->set_level(spdlog::level::info);
    return logger;
  }

 public:
  explicit FileWriterThread(Torrent& torrent, TorrentWrittenCallback cb = {})
      : m_logger(init_logger()),
        m_file_writer(std::move(cb)),
        m_file_writer_thread([this]() { m_file_writer.run(); }) {
    torrent.set_piece_callback(bind(&FileWriter::add, &m_file_writer, _1, _2));
  }

  ~FileWriterThread() {
    m_logger->debug("FileWriter stopping");
    m_file_writer.stop();
    m_file_writer_thread.join();
  }

 private:
  std::shared_ptr<spdlog::logger> m_logger{};
  FileWriter m_file_writer;
  std::thread m_file_writer_thread;
};

}  // namespace zit
