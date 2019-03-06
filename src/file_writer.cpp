// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

#include <cstdio>
#include <fstream>

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
    m_queue.pop();
  }

  auto [torrent, piece] = t_piece;
  try {
    auto tmpfile_name = torrent->tmpfile();
    auto length = numeric_cast<uintmax_t>(torrent->length());

    // Create zeroed file if not existing
    if (!fs::exists(tmpfile_name)) {
      m_logger->info("Creating tmpfile {} for '{}' with size {}", tmpfile_name,
                     torrent->name(), length);
      {
        ofstream tmpfile(tmpfile_name, ios::binary | ios::out);
        tmpfile.exceptions(ofstream::failbit | ofstream::badbit);
      }
      filesystem::resize_file(tmpfile_name, length);
    }

    // Verify correct file size
    auto fsize = fs::file_size(tmpfile_name);
    if (fsize != length) {
      throw runtime_error("Unexpected (A) file size " + to_string(fsize));
    }

    {
      // Open and write piece at corrext offset
      auto tmpfile = fstream(tmpfile_name);
      tmpfile.exceptions(fstream::failbit | fstream::badbit);
      tmpfile.seekp(piece->id() * torrent->piece_length());
      auto data = piece->data();
      // TODO: Can we just use operator<< ?
      tmpfile.write(reinterpret_cast<char*>(data.data()),
                    numeric_cast<streamsize>(data.size()));
    }

    fsize = fs::file_size(tmpfile_name);
    if (fsize != length) {
      throw runtime_error("Unexpected (B) file size " + to_string(fsize));
    }
    m_logger->debug("Wrote piece {} for '{}'", piece->id(), torrent->name());
    // TODO: Notify torrent that piece was written
  } catch (const exception& err) {
    // TODO: Retry later ? Mark torrent as errored ?
    m_logger->error(
        "write_next_piece failed for piece {} and torrent '{}' with error: {}",
        piece->id(), torrent->name(), err.what());
  }
}

}  // namespace zit
