// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

#include <cstdio>

namespace zit {

using namespace std;
namespace fs = filesystem;

void FileWriter::add(Torrent* torrent, const shared_ptr<Piece>& piece) {
  lock_guard<mutex> lock(m_mutex);
  m_queue.push(make_tuple(torrent, piece));
  m_condition.notify_one();
}

void FileWriter::run() {
  m_logger->info("FileWriter starting");
  while (!m_stop) {
    write_next_piece();
  }
  m_logger->info("FileWriter done");
}

void FileWriter::write_next_piece() {
  TorrentPiece t_piece;
  {
    unique_lock<mutex> lock(m_mutex);
    // This releasess the lock until we are notified
    m_condition.wait(lock, [this]() { return !m_queue.empty() || m_stop; });
    if (m_stop) {
      return;
    }
    t_piece = m_queue.front();
  }

  auto [torrent, piece] = t_piece;
  try {
    auto tmpfile_name = torrent->tmpfile();
    auto length = numeric_cast<uintmax_t>(torrent->length());

    // Create zeroed file if not existing
    if (!fs::exists(tmpfile_name)) {
      m_logger->info("Creating tmpfile {} for {}", tmpfile_name,
                     torrent->name());
      fs::resize_file(tmpfile_name, length);
    }

    // Verify correct file size
    if (fs::file_size(tmpfile_name) != length) {
      throw runtime_error("Unexpected file size");
    }

    // Open and write piece at corrext offset
    auto tmpfile = fopen(tmpfile_name.c_str(), "w");
    if (tmpfile) {
      throw runtime_error("Could not open tmpfile " + tmpfile_name.string());
    }
    if (fseek(tmpfile, piece->id() * torrent->piece_length(), SEEK_SET)) {
      fclose(tmpfile);
      throw runtime_error("fseek failed");
    }
    auto data = piece->data();
    if (fwrite(data.data(), sizeof data[0], data.size(), tmpfile) !=
        data.size()) {
      fclose(tmpfile);
      throw runtime_error("fwrite failed");
    }
    fclose(tmpfile);
    m_logger->debug("Wrote piece {} for {}", piece->id(), torrent->name());
    // TODO: Notify torrent that piece was written
  } catch (const exception& err) {
    // TODO: Retry later ? Mark torrent as errored ?
    m_logger->error("write_next_piece failed for {} with error: {}",
                    torrent->name(), err.what());
  }
}

}  // namespace zit
