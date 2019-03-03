// -*- mode:c++; c-basic-offset : 2; -*-
#include "file_writer.h"

namespace zit {

using namespace std;

void FileWriter::add(shared_ptr<Torrent> torrent, shared_ptr<Piece> piece) {
  lock_guard<mutex> lock(m_mutex);
  m_queue.push(make_tuple(torrent, piece));
  m_condition.notify_one();
}

void FileWriter::write_next_piece() {
  TorrentPiece t_piece;
  {
    unique_lock<mutex> lock(m_mutex);
    while (m_queue.empty()) {
      m_condition.wait(lock);
    }
    t_piece = m_queue.front();
  }
  // TODO: Write piece to disk
}

}  // namespace zit
