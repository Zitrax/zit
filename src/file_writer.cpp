// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

#include "sha1.h"
#include "string_utils.h"

#include <cstdio>
#include <fstream>

namespace zit {

using namespace std;
namespace fs = filesystem;

void FileWriter::add(Torrent* torrent, const shared_ptr<Piece>& piece) {
  lock_guard<mutex> lock(m_queue_mutex);
  m_logger->debug("Piece {} added to queue", piece->id());
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

bytes FileWriter::read_block(uint32_t offset,
                             uint32_t length,
                             const filesystem::path& filename) {
  unique_lock<mutex> lock(m_file_mutex);
  ifstream is(filename, ios::binary);
  is.exceptions(fstream::failbit | fstream::badbit);
  is.seekg(offset);
  byte data[length];
  is.read(reinterpret_cast<char*>(data), length);
  return bytes(data, data + length);
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
      auto tmpfile = fstream(tmpfile_name, ios::in | ios::out | ios::binary);
      tmpfile.exceptions(fstream::failbit | fstream::badbit);
      auto offset = piece->id() * torrent->piece_length();
      tmpfile.seekp(offset);
      auto data = piece->data();
      if (data.size() != piece->piece_size()) {
        throw runtime_error("Unexpected size: " + to_string(data.size()) +
                            " != " + to_string(piece->piece_size()));
      }
      tmpfile.write(reinterpret_cast<char*>(data.data()),
                    numeric_cast<streamsize>(data.size()));
      m_logger->debug("Writing: {} -> {} ({})", offset, offset + data.size(),
                      data.size());
    }

    fsize = fs::file_size(tmpfile_name);
    if (fsize != length) {
      throw runtime_error("Unexpected (B) file size " + to_string(fsize));
    }
    piece->set_piece_written(true);
    m_logger->info("Wrote piece {} for '{}'", piece->id(), torrent->name());

    if (torrent->done()) {
      m_logger->info("Final piece written");
      filesystem::rename(tmpfile_name, torrent->name());
      if (m_torrent_written_callback) {
        m_torrent_written_callback(*torrent);
      }
    }
  } catch (const exception& err) {
    // TODO: Retry later ? Mark torrent as errored ?
    m_logger->error(
        "write_next_piece failed for piece {} and torrent '{}' with error: {}",
        piece->id(), torrent->name(), err.what());
  }
}

}  // namespace zit
