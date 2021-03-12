// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

#include "sha1.h"
#include "string_utils.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

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
                             const filesystem::path& filename) {
  unique_lock<mutex> lock(m_file_mutex);
  ifstream is(filename, ios::binary);
  is.exceptions(fstream::failbit | fstream::badbit);
  is.seekg(offset);
  bytes data;
  data.resize(length);
  is.read(reinterpret_cast<char*>(data.data()), length);
  return data;
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
      logger()->info("Creating tmpfile {} for '{}' with size {}", tmpfile_name,
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
      // Open and write piece at correct offset
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
      logger()->debug("Writing: {} -> {} ({})", offset, offset + data.size(),
                      data.size());
    }

    fsize = fs::file_size(tmpfile_name);
    if (fsize != length) {
      throw runtime_error("Unexpected (B) file size " + to_string(fsize));
    }
    piece->set_piece_written(true);
    logger()->info("Wrote piece {} for '{}'", piece->id(), torrent->name());

    if (torrent->done()) {
      logger()->info("Final piece written");

      if (torrent->is_single_file()) {
        filesystem::rename(tmpfile_name, torrent->name());
      } else {
        //
        // Note: This is not optimal. We will use twice the
        //       disk space. Should eventually change this
        //       to create the target files already from the
        //       beginning to avoid that.
        //
        logger()->info("Writing destination files");
        // FIXME: Error handling. What to do when we throw below?
        ifstream src(tmpfile_name, ios::binary);
        src.exceptions(ofstream::failbit | ofstream::badbit);
        bytes buf;
        // FIXME: Sensible value?
        constexpr long buflen = 0xFFFF;
        buf.resize(buflen);
        for (const auto& fi : torrent->files()) {
          const auto dst_name = torrent->name() / fi.path();
          logger()->info("  Writing {}", dst_name);
          fs::create_directory(torrent->name());
          ofstream dst(dst_name, ios::binary | ios::out);
          dst.exceptions(ofstream::failbit | ofstream::badbit);
          auto rem = fi.length();
          while (rem > 0) {
            const auto len = std::min(rem, buflen);
            logger()->debug("    Writing {} bytes to {}", len, dst_name);
            src.read(reinterpret_cast<char*>(buf.data()), len);
            dst.write(reinterpret_cast<char*>(buf.data()), len);
            rem -= len;
          }
        }
        fs::remove(tmpfile_name);
      }
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
