// -*- mode:c++; c-basic-offset : 2; -*-
#include "torrent.h"
#include "bencode.h"
#include "file_utils.h"
#include "peer.h"
#include "sha1.h"
#include "string_utils.h"
#include "timer.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <execution>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace bencode;
using namespace std;

namespace zit {

/**
 * To make transform calls more readable
 */
template <class In, class Out, class Op>
static auto transform_all(const In& in, Out& out, Op func) {
  return transform(begin(in), end(in), back_inserter(out), func);
}

/**
 * Convenience function for converting a known BeDict element for the multi
 * file "files" part.
 */
static auto beDictToFileInfo(const Element& element) {
  const auto& dict = element.template to<TypedElement<BeDict>>()->val();
  string md5;
  if (dict.find("md5sum") != dict.end()) {
    md5 = dict.at("md5sum")->template to<TypedElement<string>>()->val();
  }
  auto path_list = dict.at("path")->template to<TypedElement<BeList>>()->val();
  filesystem::path path;
  for (const auto& elm : path_list) {
    path /= elm->template to<TypedElement<string>>()->val();
  }
  return FileInfo(
      dict.at("length")->template to<TypedElement<int64_t>>()->val(), path,
      md5);
}

Torrent::Torrent(const filesystem::path& file, std::filesystem::path data_dir)
    : m_data_dir(std::move(data_dir)) {
  m_logger = spdlog::get("console");
  auto root = bencode::decode(read_file(file));

  const auto& root_dict = root->to<TypedElement<BeDict>>()->val();

  // Required
  m_announce = root_dict.at("announce")->to<TypedElement<string>>()->val();
  const auto& info = root_dict.at("info")->to<TypedElement<BeDict>>()->val();

  m_name = m_data_dir / info.at("name")->to<TypedElement<string>>()->val();
  m_tmpfile = m_name + ".zit_downloading";
  m_logger->debug("Using tmpfile {} for {}", m_tmpfile, file);
  auto pieces = info.at("pieces")->to<TypedElement<string>>()->val();
  if (pieces.size() % 20) {
    throw runtime_error("Unexpected pieces length");
  }
  for (string::size_type i = 0; i < pieces.size(); i += 20) {
    m_pieces.push_back(Sha1::fromBuffer(pieces, i));
  }
  m_piece_length = numeric_cast<uint32_t>(
      info.at("piece length")->to<TypedElement<int64_t>>()->val());

  // Either 'length' or 'files' is needed depending on mode
  if (info.find("length") != info.end()) {
    m_length = info.at("length")->to<TypedElement<int64_t>>()->val();
  }
  if (info.find("files") != info.end()) {
    if (m_length != 0) {
      throw runtime_error("Invalid torrent: dual mode");
    }
    transform_all(info.at("files")->to<TypedElement<BeList>>()->val(), m_files,
                  [](const auto& dict) { return beDictToFileInfo(*dict); });
  }
  if (m_length == 0 && m_files.empty()) {
    throw runtime_error("Invalid torrent: no mode");
  }

  auto has = [&root_dict](const auto& key) {
    return root_dict.find(key) != root_dict.end();
  };

  // Optional
  if (has("creation date")) {
    m_creation_date =
        root_dict.at("creation date")->to<TypedElement<int64_t>>()->val();
  }
  if (has("comment")) {
    m_comment = root_dict.at("comment")->to<TypedElement<string>>()->val();
  }
  if (has("created by")) {
    m_created_by =
        root_dict.at("created by")->to<TypedElement<string>>()->val();
  }
  if (has("encoding")) {
    m_encoding = root_dict.at("encoding")->to<TypedElement<string>>()->val();
  }
  if (has("md5sum")) {
    m_md5sum = root_dict.at("md5sum")->to<TypedElement<string>>()->val();
  }
  if (has("private")) {
    m_private =
        root_dict.at("private")->to<TypedElement<int64_t>>()->val() == 1;
  }
  if (has("announce-list")) {
    const auto& announce_list =
        root_dict.at("announce-list")->to<TypedElement<BeList>>()->val();

    transform_all(announce_list, m_announce_list, [](const auto& tier) {
      vector<string> tier_output_list;
      transform_all(tier->template to<TypedElement<BeList>>()->val(),
                    tier_output_list, [](const auto& elm) {
                      return elm->template to<TypedElement<string>>()->val();
                    });
      return tier_output_list;
    });
  }

  m_info_hash = Sha1::calculateData(encode(info));

  // If we already have a file - scan it and mark what pieces we already have
  verify_existing_file();
}

void Torrent::verify_existing_file() {
  bool full_file = false;
  if (filesystem::exists(m_name)) {
    if (filesystem::exists(m_tmpfile)) {
      throw runtime_error("Temporary and full filename exists");
    }
    // Assume that we have the whole file
    m_tmpfile = m_name;
    full_file = true;
  }
  if (filesystem::exists(m_tmpfile)) {
    std::atomic_uint32_t pieces = 0;
    std::mutex mutex;
    if (is_single_file()) {
      m_logger->info("Verifying existing file: {}", m_tmpfile);
      Timer timer("verifying existing file");
      const auto length = filesystem::file_size(m_tmpfile);
      // Verify each piece in parallel to speed it up
      std::for_each(
          std::execution::par_unseq, m_pieces.begin(), m_pieces.end(),
          [&length, &pieces, &mutex,
           this  // Be careful what is used from this. It needs to be thread
                 // safe.
      ](const Sha1& sha1) {
            const auto id = zit::numeric_cast<uint32_t>(&sha1 - &m_pieces[0]);
            const auto offset = id * m_piece_length;
            ifstream is{m_tmpfile, ios::in | ios::binary};
            is.exceptions(ifstream::failbit | ifstream::badbit);
            is.seekg(offset);
            const auto tail = zit::numeric_cast<uint32_t>(length - offset);
            const auto len = std::min(m_piece_length, tail);
            bytes data(len);
            is.read(reinterpret_cast<char*>(data.data()), len);
            const auto fsha1 = Sha1::calculateData(data);
            if (sha1 == fsha1) {
              // Lock when updating pieces, inserting into std::map is likely
              // not thread safe.
              std::lock_guard<std::mutex> lock(mutex);
              m_client_pieces[id] = true;
              m_active_pieces.emplace(id,
                                      make_shared<Piece>(id, m_piece_length));
              m_active_pieces[id]->set_piece_written(true);
              ++pieces;
            } else {
              m_logger->trace("Piece {} does not match ({}!={})", id, sha1,
                              fsha1);
            }
          });
    } else {
      m_logger->info("Verifying existing files in: {}", m_tmpfile);
      const auto global_len = length();
      // Verify each piece in parallel to speed it up
      std::for_each(
          std::execution::par_unseq, m_pieces.begin(), m_pieces.end(),
          [&pieces, &mutex, &global_len,
           this  // Be careful what is used from this. It needs to be
                 // thread safe.
      ](const Sha1& sha1) {
            const auto id = zit::numeric_cast<uint32_t>(&sha1 - &m_pieces[0]);
            const auto pos = id * m_piece_length;
            // The piece might be spread over more than one file
            bytes data(m_piece_length, 0_b);
            auto remaining = numeric_cast<int64_t>(m_piece_length);
            int64_t gpos = pos;  // global pos
            int64_t ppos = 0;    // piece pos
            while (remaining > 0 && gpos < global_len) {
              const auto& [fi, offset, left] = file_at_pos(gpos);
              ifstream is{name() / fi.path(), ios::in | ios::binary};
              is.exceptions(ifstream::failbit | ifstream::badbit);
              is.seekg(offset);
              const auto len = std::min(left, remaining);
              is.read(reinterpret_cast<char*>(std::next(data.data(), ppos)),
                      len);
              gpos += len;
              ppos += len;
              remaining -= len;
            }
            // Since last piece might not be filled
            data.resize(data.size() - numeric_cast<size_t>(remaining));
            const auto fsha1 = Sha1::calculateData(data);
            if (sha1 == fsha1) {
              // Lock when updating pieces, inserting into std::map is likely
              // not thread safe.
              std::lock_guard<std::mutex> lock(mutex);
              m_client_pieces[id] = true;
              m_active_pieces.emplace(id,
                                      make_shared<Piece>(id, m_piece_length));
              m_active_pieces[id]->set_piece_written(true);
              ++pieces;
            } else {
              m_logger->debug("Piece {} does not match ({}!={})", id, sha1,
                              fsha1);
            }
          });
    }
    m_logger->info("Verification done. {}/{} pieces done.", pieces,
                   m_pieces.size());
    if (full_file && (pieces != m_pieces.size())) {
      throw runtime_error("Filename exists but does not match all pieces");
    }
  }
}

int64_t Torrent::length() const {
  return is_single_file()
             ? m_length
             : accumulate(
                   m_files.cbegin(), m_files.cend(), static_cast<int64_t>(0),
                   [](int64_t a, const FileInfo& b) { return a + b.length(); });
}

auto Torrent::left() const {
  // FIXME: Should not include pieces that are done

  if (is_single_file()) {
    return length() - accumulate(m_active_pieces.begin(), m_active_pieces.end(),
                                 static_cast<int64_t>(0),
                                 [](int64_t a, const auto& p) {
                                   return a + (p.second->piece_written()
                                                   ? p.second->piece_size()
                                                   : 0);
                                 });
  }

  return accumulate(
      m_files.begin(), m_files.end(), static_cast<int64_t>(0),
      [](int64_t a, const FileInfo& b) { return a + b.length(); });
}

void Torrent::disconnected(Peer* peer) {
  if (m_disconnect_callback) {
    m_disconnect_callback(peer);
  }
}

void Torrent::start() {
  if (!m_peers.empty()) {
    throw runtime_error("Local peer vector not empty");
  }

  Url url(m_announce);
  url.add_param("info_hash=" + Net::urlEncode(m_info_hash));
  // FIXME: Use proper id - should be unique per peer thus not fixed
  url.add_param("peer_id=abcdefghijklmnopqrst");
  url.add_param("port=" + to_string(m_listening_port));
  url.add_param("uploaded=0");
  url.add_param("downloaded=0");
  url.add_param("left=" + to_string(left()));
  url.add_param("event=started");
  url.add_param("compact=1");  // TODO: Look up what this really means
  m_logger->info("\n{}", url);

  auto [headers, body] = Net::httpGet(url);

  ElmPtr reply;
  try {
    reply = decode(body);
  } catch (const BencodeConversionError&) {
    throw_with_nested(runtime_error("Could not decode peer list."));
  }

  m_logger->debug("=====HEADER=====\n{}\n=====BODY=====\n{}", headers, reply);

  auto reply_dict = reply->to<TypedElement<BeDict>>()->val();
  if (reply_dict.find("peers") == reply_dict.end()) {
    throw runtime_error("Invalid tracker reply, no peer list");
  }
  auto peers_dict = reply_dict["peers"];
  // The peers might be in binary or string form
  // First try string form ...
  if (peers_dict->is<TypedElement<BeList>>()) {
    m_logger->debug("Peer list in string form");
    const auto peer_list = peers_dict->to<TypedElement<BeList>>()->val();
    for (const auto& elm : peer_list) {
      const auto peer = elm->to<TypedElement<BeDict>>()->val();
      const auto purl = Url(fmt::format(
          "http://{}:{}", peer.at("ip")->to<TypedElement<std::string>>()->val(),
          peer.at("port")->to<TypedElement<int64_t>>()->val()));
      m_peers.emplace_back(make_shared<Peer>(purl, *this));
    }
  } else {
    // ... else binary form
    auto binary_peers = peers_dict->to<TypedElement<string>>()->val();
    m_logger->debug("Peer list in binary form");
    if (binary_peers.empty()) {
      throw runtime_error("Peer list is empty");
    }

    const int THREE_HEX_BYTES = 6;
    for (unsigned long i = 0; i < binary_peers.length(); i += THREE_HEX_BYTES) {
      const auto purl = Url(binary_peers.substr(i, THREE_HEX_BYTES), true);
      if (!(purl.host() == "127.0.0.1" && purl.port() == m_listening_port)) {
        m_peers.emplace_back(make_shared<Peer>(purl, *this));
      }
    }
  }

  // Handshake with all the remote peers
  for (auto& p : m_peers) {
    p->handshake();
  }

  // Add listening peer for incoming connections
  m_logger->info("Adding listening peer");
  try {
    m_peers.emplace_back(make_shared<Peer>(*this))->listen();
  } catch (const std::exception& ex) {
    m_logger->warn("Could not start listening peer: {}", ex.what());
  }
}

void Torrent::run() {
  m_logger->debug("Run loop start");
  while (!all_of(m_peers.begin(), m_peers.end(),
                 [](auto& p) { return p->io_service().stopped(); })) {
    // m_logger->debug("Run loop");
    std::size_t ran = 0;
    for (auto& p : m_peers) {
      ran += p->io_service().poll_one();
    }
    retry_pieces();
    // If no handlers ran, then sleep.
    if (!ran) {
      this_thread::sleep_for(10ms);
    }
  }
  m_logger->debug("Run loop done");
}

void Torrent::stop() {
  for (auto& peer : m_peers) {
    peer->stop();
  }
}

static auto now() {
  return std::chrono::system_clock::now();
}

void Torrent::retry_pieces() {
  // Do not need to call this too frequently so rate limit it
  static std::chrono::system_clock::time_point last_call{now() + 1min};

  if (now() - last_call > 30s) {
    last_call = now();
    m_logger->debug("Checking pieces for retry");
    std::size_t retry = 0;
    for (auto& [id, piece] : m_active_pieces) {
      retry += piece->retry_blocks();
    }
    m_logger->trace("retry count = {}", retry);
    if (!retry) {
      return;
    }
    m_logger->info("Marked {} blocks for retry", retry);

    // To hit different peers for each invocation - shuffle the list
    std::random_device rd;
    std::mt19937 g(rd());
    shuffle(m_peers.begin(), m_peers.end(), g);

    auto it = m_peers.begin();
    if (it == m_peers.end()) {
      m_logger->warn("No peers available for retrying");
      return;
    }
    auto start_count = retry;
    while (retry > 0) {
      retry -= (*it)->request_next_block(1);
      it++;
      if (it == m_peers.end()) {
        if (retry == start_count) {
          m_logger->warn("Could not retry all blocks.");
          break;
        }
        start_count = retry;
        it = m_peers.begin();
      }
    }
  }
}

bool Torrent::set_block(uint32_t piece_id, uint32_t offset, const bytes& data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Look up relevant piece object among active pieces
  if (m_active_pieces.find(piece_id) != m_active_pieces.end()) {
    auto piece = m_active_pieces[piece_id];
    if (piece->set_block(offset, data)) {
      m_logger->debug("Piece {} done!", piece_id);
      piece_done(piece);
    }
    return true;
  }
  m_logger->warn("Tried to set block for non active piece");
  return false;
}

std::shared_ptr<Piece> Torrent::active_piece(uint32_t id, bool create) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto piece = m_active_pieces.find(id);
  if (piece == m_active_pieces.end()) {
    if (!create) {
      return nullptr;
    }
    int64_t piece_length = m_piece_length;
    if (id == pieces().size() - 1) {
      // Last piece
      auto mod = length() % m_piece_length;
      piece_length = mod ? mod : m_piece_length;
      m_logger->debug("Last piece {} with length = {}", id, piece_length);
    }
    auto it = m_active_pieces.emplace(
        make_pair(id, make_shared<Piece>(id, piece_length)));
    return it.first->second;
  }
  return m_active_pieces.at(id);
}

bool Torrent::done() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  // If we haven't started on all pieces we are not done
  if (m_active_pieces.size() != m_pieces.size()) {
    return false;
  }

  // If any piece has not been written we are not done
  for (const auto& piece : m_active_pieces) {
    if (!piece.second->piece_written()) {
      return false;
    }
  }

  return true;
}

std::tuple<FileInfo, int64_t, int64_t> Torrent::file_at_pos(int64_t pos) const {
  int64_t cpos = 0;
  for (const auto& fi : files()) {
    if (pos < (cpos + fi.length())) {
      return make_tuple(fi, pos - cpos, cpos + fi.length() - pos);
    }
    cpos += fi.length();
  }
  throw runtime_error(fmt::format("pos > torrent size {}>{}", pos, length()));
}

void Torrent::piece_done(std::shared_ptr<Piece>& piece) {
  m_client_pieces[piece->id()] = true;
  m_piece_callback(this, piece);
}

ostream& operator<<(ostream& os, const zit::FileInfo& file_info) {
  os << "(" << file_info.path() << ", " << file_info.length();
  auto md5 = file_info.md5sum();
  if (!md5.empty()) {
    os << ", " << file_info.md5sum();
  }
  os << ")";
  return os;
}

ostream& operator<<(ostream& os, const zit::Torrent& torrent) {
  os << "----------------------------------------\n";
  auto creation = numeric_cast<time_t>(torrent.creation_date());
  os << "Creation date: " << put_time(localtime(&creation), "%F %T %Z") << " ("
     << creation << ")\n";
  os << "Comment:       " << torrent.comment() << "\n";
  if (!torrent.created_by().empty()) {
    os << "Created by:    " << torrent.created_by() << "\n";
  }
  if (!torrent.encoding().empty()) {
    os << "Encoding:      " << torrent.encoding() << "\n";
  }
  os << "Piece length:  " << torrent.piece_length() << "\n";
  os << "Private:       " << (torrent.is_private() ? "Yes" : "No") << "\n";
  if (torrent.is_single_file()) {
    os << "Name:          " << torrent.name() << "\n";
    os << "Length:        " << torrent.length() << "\n";
    if (!torrent.md5sum().empty()) {
      os << "MD5Sum:        " << torrent.md5sum() << "\n";
    }
  } else {
    os << "Files:\n";
    for (const auto& fi : torrent.files()) {
      os << "               " << fi;
    }
  }
  os << "Announce List:\n";
  for (const auto& list : torrent.announce_list()) {
    for (const auto& url : list) {
      os << "               " << url << "\n";
    }
  }
  if (torrent.announce_list().empty()) {
    os << "               " << torrent.announce() << "\n";
  }
  os << "----------------------------------------\n";
  return os;
}

}  // namespace zit
