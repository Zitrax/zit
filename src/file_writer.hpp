// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include "torrent.hpp"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <tuple>

#include "spdlog/sinks/stdout_color_sinks.h"

namespace zit {

using TorrentPiece = std::tuple<Torrent*, std::shared_ptr<Piece>>;
using TorrentWrittenCallback = std::function<void(Torrent&)>;

static auto init_logger() {
  auto logger = spdlog::get("file_writer");
  if (!logger) {
    logger = spdlog::stdout_color_mt("file_writer");
  }
  return logger;
}

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
 private:
  FileWriter() = default;
  static auto logger() { return init_logger(); }

 public:
  static FileWriter& getInstance() {
    static FileWriter instance;
    return instance;
  }

  // Delete copy and assignment since this is a singleton
  FileWriter(FileWriter const&) = delete;
  void operator=(FileWriter const&) = delete;

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
   * Call this to stop the FileWriter. It will stop after finishing current
   * write.
   */
  void stop() {
    m_stop = true;
    m_condition.notify_one();
  }

  /**
   * Will read some bytes from the file. Call can block until the
   * the writer thread is done with current work.
   *
   * @param offset the offset into the file to read from
   * @param length the number of bytes to read
   * @param filename the path to the file to read from
   */
  bytes read_block(uint32_t offset, uint32_t length, const Torrent& filename);

  /**
   * A callback called when the whole torrent is finished (final piece written)
   */
  void set_callback(TorrentWrittenCallback cb) {
    m_torrent_written_callback = std::move(cb);
  }

 private:
  void write_next_piece();

  std::queue<TorrentPiece> m_queue{};
  std::mutex m_queue_mutex{};
  std::mutex m_file_mutex{};
  std::condition_variable m_condition{};
  std::atomic_bool m_stop = false;
  TorrentWrittenCallback m_torrent_written_callback{};
};

/**
 * Managing the thread running FileWriter
 */
class FileWriterThread {
 public:
  explicit FileWriterThread(TorrentWrittenCallback cb = {})
      : m_logger(init_logger()),
        m_file_writer(FileWriter::getInstance()),
        m_file_writer_thread([this]() { m_file_writer.run(); }) {
    m_file_writer.set_callback(std::move(cb));
  }

  /**
   * Each torrent needs to be registered to hook up file writing.
   */
  void register_torrent(Torrent& torrent) {
    torrent.add_piece_callback(
        [&](Torrent* t, const std::shared_ptr<Piece>& piece) {
          m_file_writer.add(t, piece);
        });
  }

  ~FileWriterThread() {
    m_logger->debug("FileWriter stopping");
    m_file_writer.stop();
    m_file_writer_thread.join();
    m_logger->debug("FileWriter stopped");
  }

 private:
  std::shared_ptr<spdlog::logger> m_logger{};
  FileWriter& m_file_writer;
  std::thread m_file_writer_thread;
};

}  // namespace zit
