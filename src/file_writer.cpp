// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

#include "sha1.h"
#include "string_utils.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace zit {

using namespace std;
namespace fs = filesystem;

void FileWriter::add(Torrent* torrent, const shared_ptr<Piece>& piece) {
  lock_guard<mutex> lock(m_queue_mutex);
  logger()->debug("Piece {} added to queue", piece->id());
  m_queue.push(make_tuple(torrent, piece));
  m_condition.notify_one();
}

void FileWriter::run() {
  logger()->info("FileWriter starting");
  while (!m_stop) {
    write_next_piece();
  }
  logger()->info("FileWriter done");
}

bytes FileWriter::read_block(uint32_t offset,
                             uint32_t length,
                             const fs::path& filename) {
  unique_lock<mutex> lock(m_file_mutex);
  ifstream is(filename, ios::binary);
  is.exceptions(fstream::failbit | fstream::badbit);
  is.seekg(offset);
  bytes data;
  data.resize(length);
  is.read(reinterpret_cast<char*>(data.data()), length);
  return data;
}

/**
 * Interface for writing pieces to destination files. Subclasses
 * implements this for single and multi torrents.
 */
class TorrentDestination {
 public:
  explicit TorrentDestination(const Torrent* torrent,
                              shared_ptr<spdlog::logger> logger)
      : m_torrent(torrent), m_logger(move(logger)) {}
  virtual ~TorrentDestination() = default;

  // Special member functions (hicpp-special-member-functions)
  TorrentDestination(const TorrentDestination& other) = delete;
  TorrentDestination(TorrentDestination&& other) noexcept = delete;
  TorrentDestination& operator=(const TorrentDestination& rhs) = delete;
  TorrentDestination& operator=(TorrentDestination&& other) noexcept = delete;

  /** Allocate space on disk for the torrent download. */
  virtual void allocate() = 0;

  /** Check that the files on disk match the torrent. */
  virtual void verifyFileSize() = 0;

  /** Actually write piece to disk */
  virtual void writePiece(const Piece& piece) = 0;

  /** Mark file as done */
  virtual void torrentComplete() = 0;

  static shared_ptr<TorrentDestination> create(
      const Torrent* torrent,
      shared_ptr<spdlog::logger> logger);

  [[nodiscard]] auto torrent() const { return m_torrent; }
  [[nodiscard]] auto logger() const { return m_logger; }

 private:
  const Torrent* m_torrent;
  shared_ptr<spdlog::logger> m_logger;
};

class SingleTorrentDestination : public TorrentDestination {
 public:
  SingleTorrentDestination(const Torrent* torrent,
                           shared_ptr<spdlog::logger> logger)
      : TorrentDestination(torrent, move(logger)) {}

  void allocate() override {
    const auto tmpfile_name = torrent()->tmpfile();
    if (!fs::exists(tmpfile_name)) {
      const auto length = numeric_cast<uintmax_t>(torrent()->length());
      logger()->info("Creating tmpfile {} for '{}' with size {}", tmpfile_name,
                     torrent()->name(), length);
      {
        ofstream tmpfile(tmpfile_name, ios::binary | ios::out);
        tmpfile.exceptions(ofstream::failbit | ofstream::badbit);
      }
      fs::resize_file(torrent()->tmpfile(), length);
    }
  }

  void verifyFileSize() override {
    auto fsize = fs::file_size(torrent()->tmpfile());
    if (fsize != numeric_cast<uintmax_t>(torrent()->length())) {
      throw runtime_error("Unexpected (A) file size " + to_string(fsize));
    }
  }

  void writePiece(const Piece& piece) override {
    auto tmpfile =
        fstream(torrent()->tmpfile(), ios::in | ios::out | ios::binary);
    tmpfile.exceptions(fstream::failbit | fstream::badbit);
    auto offset = piece.id() * torrent()->piece_length();
    tmpfile.seekp(offset);
    auto data = piece.data();
    if (data.size() != piece.piece_size()) {
      throw runtime_error("Unexpected size: " + to_string(data.size()) +
                          " != " + to_string(piece.piece_size()));
    }
    tmpfile.write(reinterpret_cast<char*>(data.data()),
                  numeric_cast<streamsize>(data.size()));
    logger()->debug("Writing: {} -> {} ({})", offset, offset + data.size(),
                    data.size());
  }

  void torrentComplete() override {
    fs::rename(torrent()->tmpfile(), torrent()->name());
  }
};

class MultiTorrentDestination : public TorrentDestination {
 public:
  MultiTorrentDestination(const Torrent* torrent,
                          shared_ptr<spdlog::logger> logger)
      : TorrentDestination(torrent, move(logger)) {}

  void allocate() override {
    // TODO: Rename all files instead?
    if (!fs::exists(torrent()->tmpfile())) {
      {
        ofstream tmpfile(torrent()->tmpfile(), ios::binary | ios::out);
        tmpfile.exceptions(ofstream::failbit | ofstream::badbit);
      }
      fs::resize_file(torrent()->tmpfile(), 1);
      fs::create_directory(torrent()->name());
      for (const auto& fi : torrent()->files()) {
        const auto dst_name = torrent()->name() / fi.path();
        logger()->info("  Creating {} with size {}", dst_name, fi.length());
        ofstream dst(dst_name, ios::binary | ios::out);
        dst.exceptions(ofstream::failbit | ofstream::badbit);
        fs::resize_file(dst_name, numeric_cast<std::uintmax_t>(fi.length()));
      }
    }
  }

  void verifyFileSize() override {
    const auto files = torrent()->files();
    if (!all_of(files.begin(), files.end(), [&](const auto& fi) {
          const auto expected = numeric_cast<uintmax_t>(fi.length());
          const auto actual = fs::file_size(torrent()->name() / fi.path());
          return actual == expected;
        })) {
      throw runtime_error("Unexpected multi torrent file size");
    }
  }

  void writePiece(const Piece& piece) override {
    // The piece might be spread over more than one file
    auto remaining = numeric_cast<int64_t>(piece.piece_size());
    while (remaining > 0) {
      const auto done = piece.piece_size() - remaining;
      const auto pos =
          numeric_cast<int64_t>(piece.id() * torrent()->piece_length() + done);
      const auto [fi, offset, left] = torrent()->file_at_pos(pos);
      const auto len = min(remaining, left);
      logger()->debug("Writing: {} -> {} ({}) done={} offset={} (In: {})", pos,
                      pos + len, len, done, offset, fi.path());
      auto tmpfile = fstream(torrent()->name() / fi.path(),
                             ios::in | ios::out | ios::binary);
      tmpfile.exceptions(fstream::failbit | fstream::badbit);
      tmpfile.seekp(offset);
      auto data = piece.data();
      assert(numeric_cast<uint64_t>(done) < data.size());
      tmpfile.write(reinterpret_cast<char*>(next(data.data(), done)),
                    numeric_cast<streamsize>(len));
      remaining -= len;
    }
  }

  void torrentComplete() override { fs::remove(torrent()->tmpfile()); }
};

shared_ptr<TorrentDestination> TorrentDestination::create(
    const Torrent* torrent,
    shared_ptr<spdlog::logger> logger) {
  if (torrent->is_single_file()) {
    return make_shared<SingleTorrentDestination>(torrent, move(logger));
  }
  return make_shared<MultiTorrentDestination>(torrent, move(logger));
}

void FileWriter::write_next_piece() {
  TorrentPiece t_piece;
  {
    unique_lock<mutex> lock(m_queue_mutex);
    // This releasess the lock until we are notified
    m_condition.wait(lock, [this]() { return !m_queue.empty() || m_stop; });
    if (m_stop) {
      return;
    }
    t_piece = m_queue.front();
    m_queue.pop();
  }

  unique_lock<mutex> lock(m_file_mutex);
  auto [torrent, piece] = t_piece;
  try {
    auto sha = Sha1::calculateData(piece->data());
    if (sha != torrent->pieces()[piece->id()]) {
      throw runtime_error("Piece data does not match expected Sha1");
    }

    auto dest = TorrentDestination::create(torrent, logger());

    // Create zeroed file if not existing
    dest->allocate();

    // Verify correct file size
    dest->verifyFileSize();

    // Open and write piece at correct offset
    dest->writePiece(*piece);

    // Ensure we did not change the file size(s)
    dest->verifyFileSize();

    piece->set_piece_written(true);
    auto [have, total] = torrent->piece_status();
    logger()->info("Wrote piece {} for '{}' ({}/{})", piece->id(),
                   torrent->name(), have, total);

    if (torrent->done()) {
      logger()->info("Final piece written");
      dest->torrentComplete();
      if (m_torrent_written_callback) {
        m_torrent_written_callback(*torrent);
      }
    }
  } catch (const exception& err) {
    // TODO: Retry later ? Mark torrent as errored ?
    logger()->error(
        "write_next_piece failed for piece {} and torrent '{}' with error: {}",
        piece->id(), torrent->name(), err.what());
  }
}

}  // namespace zit
