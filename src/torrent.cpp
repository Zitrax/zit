// -*- mode:c++; c-basic-offset : 2; -*-
#include "torrent.hpp"
#include "bencode.hpp"
#include "file_utils.hpp"
#include "global_config.hpp"
#include "peer.hpp"
#include "sha1.hpp"
#include "string_utils.hpp"
#include "timer.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <execution>
#include <filesystem>
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

namespace {

/**
 * Convenience function for converting a known BeDict element for the multi
 * file "files" part.
 */
auto beDictToFileInfo(const Element& element) {
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

// Based on answers in https://stackoverflow.com/q/440133/11722
auto random_string(std::size_t len) -> std::string {
  static std::string chars =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  thread_local static std::mt19937 rg{std::random_device{}()};
  thread_local static auto dist = std::uniform_int_distribution<unsigned long>{
      {}, zit::numeric_cast<unsigned long>(chars.size()) - 1};
  std::string result(len, '\0');
  std::generate_n(begin(result), len, [&] { return chars.at(dist(rg)); });
  return result;
}

}  // namespace

// Could possibly be grouped in a struct
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Torrent::Torrent(const filesystem::path& file,
                 std::filesystem::path data_dir,
                 const Config& config,
                 HttpGet http_get)
    : m_config(config),
      m_data_dir(std::move(data_dir)),
      m_peer_id(random_string(20)),
      m_listening_port(
          numeric_cast<unsigned short>(config.get(IntSetting::LISTENING_PORT),
                                       "listening port out of range")),
      m_connection_port(
          numeric_cast<unsigned short>(config.get(IntSetting::CONNECTION_PORT),
                                       "connection port out of range")),
      m_http_get(std::move(http_get)) {
  m_logger = spdlog::get("console");
  auto root = bencode::decode(read_file(file));

  const auto& root_dict = root->to<TypedElement<BeDict>>()->val();

  // Required
  m_announce = root_dict.at("announce")->to<TypedElement<string>>()->val();
  const auto& info = root_dict.at("info")->to<TypedElement<BeDict>>()->val();

  m_name = (m_data_dir / info.at("name")->to<TypedElement<string>>()->val())
               .string();
  m_tmpfile = m_name + Torrent::tmpfileExtension();
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

uint32_t Torrent::piece_length(uint32_t id) const {
  if (id == pieces().size() - 1) {
    // The last piece might be shorter
    const auto mod = numeric_cast<uint32_t>(length() % m_piece_length);
    return mod ? mod : m_piece_length;
  }
  return m_piece_length;
}

void Torrent::verify_existing_file() {
  bool full_file = false;

  if (is_single_file() && filesystem::exists(m_name)) {
    if (filesystem::exists(m_tmpfile)) {
      throw runtime_error("Temporary and full filename exists");
    }
    // Assume that we have the whole file
    m_tmpfile = m_name;
    full_file = true;
  }

  if (!is_single_file() && filesystem::exists(m_name)) {
    // We have either started or finished this torrent
    // Ensure that we verify the content.
    m_tmpfile = m_name;
  }

  if (filesystem::exists(m_tmpfile)) {
    std::atomic_uint32_t num_pieces = 0;
    std::mutex mutex;
    if (is_single_file()) {
      m_logger->info("Verifying existing file: {}", m_tmpfile);
      Timer timer("verifying existing file");
      const auto file_length = filesystem::file_size(m_tmpfile);
      // Verify each piece in parallel to speed it up
      std::for_each(
          std::execution::par_unseq, m_pieces.begin(), m_pieces.end(),
          [&file_length, &num_pieces, &mutex,
           this  // Be careful what is used from this. It needs to be
                 // thread safe.
      ](const Sha1& sha1) {
            const auto id =
                zit::numeric_cast<uint32_t>(&sha1 - m_pieces.data());
            const auto offset = id * m_piece_length;
            ifstream is{m_tmpfile, ios::in | ios::binary};
            is.exceptions(ifstream::failbit | ifstream::badbit);
            is.seekg(offset);
            const auto tail = zit::numeric_cast<uint32_t>(file_length - offset);
            const auto len = std::min(m_piece_length, tail);
            bytes data(len);
            is.read(reinterpret_cast<char*>(data.data()), len);
            const auto fsha1 = Sha1::calculateData(data);
            if (sha1 == fsha1) {
              // Lock when updating num_pieces, inserting into
              // std::map is likely not thread safe.
              std::lock_guard<std::mutex> lock(mutex);
              m_client_pieces[id] = true;
              m_active_pieces.emplace(
                  id,
                  make_shared<Piece>(PieceId(id), PieceSize(piece_length(id))));
              m_active_pieces[id]->set_piece_written(true);
              ++num_pieces;
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
          [&num_pieces, &mutex, &global_len,
           this  // Be careful what is used from this. It needs to be
                 // thread safe.
      ](const Sha1& sha1) {
            const auto id =
                zit::numeric_cast<uint32_t>(&sha1 - m_pieces.data());
            const auto pos = id * m_piece_length;
            // The piece might be spread over more than one file
            bytes data(m_piece_length, 0_b);
            auto remaining = numeric_cast<int64_t>(m_piece_length);
            int64_t gpos = pos;  // global pos
            int64_t ppos = 0;    // piece pos
            while (remaining > 0 && gpos < global_len) {
              const auto& [fi, offset, left] = file_at_pos(gpos);
              const auto file = name() / fi.path();
              if (!filesystem::exists(file)) {
                return;
              }
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
              // Lock when updating num_pieces, inserting into std::map is
              // likely not thread safe.
              std::lock_guard<std::mutex> lock(mutex);
              m_client_pieces[id] = true;
              m_active_pieces.emplace(
                  id,
                  make_shared<Piece>(PieceId(id), PieceSize(piece_length(id))));
              m_active_pieces[id]->set_piece_written(true);
              ++num_pieces;
            } else {
              m_logger->trace("Piece {} does not match ({}!={})", id, sha1,
                              fsha1);
            }
          });
    }
    m_logger->info("Verification done. {}/{} pieces done.", num_pieces,
                   m_pieces.size());
    if (full_file && (num_pieces != m_pieces.size())) {
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

auto Torrent::downloaded() const {
  return accumulate(
      m_active_pieces.begin(), m_active_pieces.end(), static_cast<int64_t>(0),
      [](int64_t sum, const auto& piece) {
        return sum +
               (piece.second->piece_written() ? piece.second->piece_size() : 0);
      });
}

auto Torrent::left() const {
  return length() - downloaded();
}

void Torrent::disconnected(Peer* peer) {
  if (m_disconnect_callback) {
    m_disconnect_callback(peer);
  }
}

std::ostream& operator<<(std::ostream& os, const Torrent::TrackerEvent& te) {
  switch (te) {
    case Torrent::TrackerEvent::STARTED:
      os << "started";
      break;
    case Torrent::TrackerEvent::COMPLETED:
      os << "completed";
      break;
    case Torrent::TrackerEvent::STOPPED:
      os << "stopped";
      break;
    case Torrent::TrackerEvent::UNSPECIFIED:
      // The requests performed at regular intervals has no event name
      os << "";
      break;
  }
  return os;
}

namespace {

bool is_local(const auto& purl, auto port) {
  return purl.host() == "127.0.0.1" && purl.port() == port.get();
}

void read_peers_string_list(Torrent& torrent,
                            const bencode::Element& peers_dict,
                            std::vector<std::shared_ptr<Peer>>& peers) {
  const auto peer_list = peers_dict.to<TypedElement<BeList>>()->val();
  for (const auto& elm : peer_list) {
    const auto peer = elm->to<TypedElement<BeDict>>()->val();
    const auto purl = Url(fmt::format(
        "http://{}:{}", peer.at("ip")->to<TypedElement<std::string>>()->val(),
        peer.at("port")->to<TypedElement<int64_t>>()->val()));
    if (!is_local(purl, torrent.listning_port())) {
      peers.emplace_back(make_shared<Peer>(purl, torrent));
    }
  }
}

void read_peers_binary_form(Torrent& torrent,
                            const bencode::Element& peers_dict,
                            std::vector<std::shared_ptr<Peer>>& peers) {
  auto binary_peers = peers_dict.to<TypedElement<string>>()->val();
  if (binary_peers.empty()) {
    throw runtime_error("Peer list is empty");
  }

  const int THREE_HEX_BYTES = 6;
  for (unsigned long i = 0; i < binary_peers.length(); i += THREE_HEX_BYTES) {
    const auto purl = Url(binary_peers.substr(i, THREE_HEX_BYTES), true);
    if (!is_local(purl, torrent.listning_port())) {
      peers.emplace_back(make_shared<Peer>(purl, torrent));
    }
  }
}

}  // namespace

std::vector<std::shared_ptr<Peer>> Torrent::tracker_request(
    TrackerEvent event) {
  // Folllowing multi-tracker spec:
  // https://github.com/rakshasa/libtorrent/blob/master/doc/multitracker-spec.txt
  auto announce_list = [this]() -> std::vector<std::vector<std::string>> {
    if (!m_announce_list.empty()) {
      return m_announce_list;
    }
    return {{m_announce}};
  }();

  auto do_tracker_request =
      [&,
       this](const auto& announce_url) -> std::vector<std::shared_ptr<Peer>> {
    Url url(announce_url);
    url.add_param("info_hash=" + Net::urlEncode(m_info_hash));
    url.add_param(fmt::format("peer_id={}", peer_id()));
    url.add_param("port=" + to_string(m_listening_port.get()));
    url.add_param("uploaded=0");
    // TODO: According to the spec this should be the amount downloaded
    //       since the started event to the tracker. For now using the total.
    url.add_param("downloaded=" + to_string(downloaded()));
    url.add_param("left=" + to_string(left()));
    // FIXME: No one calls this with COMPLETED and STOPPED (we should)
    url.add_param("event=" + fmt::format("{}", event));
    url.add_param("compact=1");

    if (m_logger->should_log(spdlog::level::debug)) {
      m_logger->debug("Tracker request:\n{}", url);
    } else {
      m_logger->info("Tracker request: {}", url.str());
    }

    auto [headers, body] = m_http_get(url);

    std::vector<std::shared_ptr<Peer>> peers;

    // We only care about decoding the peer list for certain events
    if (event == TrackerEvent::UNSPECIFIED || event == TrackerEvent::STARTED) {
      if (!m_config.get(BoolSetting::INITIATE_PEER_CONNECTIONS) && done()) {
        m_logger->debug("Skipping peer list since the torrent is completed.");
        return {};
      }

      ElmPtr reply;
      try {
        reply = decode(body);
      } catch (const BencodeConversionError&) {
        throw_with_nested(runtime_error("Could not decode peer list."));
      }

      m_logger->debug("=====HEADER=====\n{}\n=====BODY=====\n{}", headers,
                      reply);

      auto reply_dict = reply->to<TypedElement<BeDict>>()->val();
      if (reply_dict.find("peers") == reply_dict.end()) {
        throw runtime_error("Invalid tracker reply, no peer list");
      }
      auto peers_dict = reply_dict["peers"];
      // The peers might be in binary or string form
      // First try string form ...
      if (peers_dict->is<TypedElement<BeList>>()) {
        m_logger->debug("Peer list in string form");
        read_peers_string_list(*this, *peers_dict, peers);
      } else {
        // ... else compact/binary form
        m_logger->debug("Peer list in binary form");
        read_peers_binary_form(*this, *peers_dict, peers);
      }
    }
    return peers;
  };

  // According to the spec we will now try each tier in order and within a tier
  // we shuffle the announce list.

  std::optional<std::exception> thrown;
  std::vector<std::shared_ptr<Peer>> peers;

  std::random_device rd;
  std::mt19937 g(rd());
  for (auto tier : announce_list) {
    shuffle(tier.begin(), tier.end(), g);
    for (const auto& announce_url : tier) {
      try {
        peers = do_tracker_request(announce_url);
        thrown.reset();
        break;
      } catch (const std::exception& ex) {
        m_logger->warn("{}: {}", announce_url, ex.what());
        thrown = ex;
      }
    }
    if (!peers.empty()) {
      break;
    }
  }

  if (thrown) {
    throw_with_nested(*thrown);
  }

  return peers;
}

void Torrent::start() {
  if (!m_peers.empty()) {
    throw runtime_error("Local peer vector not empty");
  }

  // Fetch a list of peers from the tracker
  m_peers = tracker_request(TrackerEvent::STARTED);

  // Handshake with all the remote peers
  m_logger->info("Starting handshake with {} peers", m_peers.size());
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
    retry_peers();
    // If no handlers ran, then sleep.
    if (!ran) {
#ifdef WIN32
      this_thread::sleep_for(10ms);
#else
      // For some reason this crashes clang-tidy 10,11,12, thus usleep
      // this_thread::sleep_for(10ms);
      usleep(10000);
#endif  // WIN32
    }
  }
  m_logger->debug("Run loop done");
}

void Torrent::stop() {
  for (auto& peer : m_peers) {
    peer->stop();
  }
  tracker_request(TrackerEvent::STOPPED);
}

static auto now() {
  return std::chrono::system_clock::now();
}

void Torrent::retry_pieces() {
  // Do not need to call this too frequently so rate limit it
  static std::chrono::system_clock::time_point last_call{now() + 1min};

  // False positive
  // NOLINTNEXTLINE(hicpp-use-nullptr,modernize-use-nullptr)
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

void Torrent::retry_peers() {
  // Do not need to call this too frequently so rate limit it
  static std::chrono::system_clock::time_point last_call{now() + 2min};

  if (now() - last_call <= 2min) {
    return;
  }

  last_call = now();
  m_logger->debug("Checking peers for retry");

  // Find and disconnect inactive peers
  // TODO: Nicer to use ranges, however for clang we need to wait for clang16
  const auto it = std::partition(m_peers.begin(), m_peers.end(), [](auto& p) {
    return p->is_inactive() && !p->is_listening();
  });

  const auto inactive = std::distance(m_peers.begin(), it);

  if (inactive) {
    m_logger->info("Stopping {} inactive peers", inactive);
    std::for_each(m_peers.begin(), it, [](auto& p) { p->stop(); });
  }

  // Connect to new peers (discarding the ones we just dropped or active ones)
  const auto tracker_peers = tracker_request(TrackerEvent::UNSPECIFIED);
  m_logger->debug("{} candidate peers", tracker_peers.size());
  std::vector<std::shared_ptr<Peer>> new_peers;

  for (const auto& tracker_peer : tracker_peers) {
    const bool in_use =
        std::find_if(m_peers.begin(), m_peers.end(),
                     [&tracker_peer](auto& existing_peer) {
                       const auto& lurl = tracker_peer->url();
                       const auto& rurl = existing_peer->url();
                       return lurl.has_value() && rurl.has_value() &&
                              tracker_peer->url().value().str() ==
                                  existing_peer->url().value().str();
                     }) != m_peers.end();
    m_logger->debug("Candidate {} was inactive: {}", tracker_peer->str(),
                    in_use);
    if (!in_use) {
      tracker_peer->handshake();
      new_peers.push_back(tracker_peer);
    }
  }

  if (!new_peers.empty()) {
    m_logger->info("Found {} new peers", new_peers.size());
  }

  if (inactive) {
    m_peers.erase(m_peers.begin(), it);
  }

  m_peers.insert(m_peers.end(), new_peers.begin(), new_peers.end());
}

// TODO: Yes, should group these in one type
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool Torrent::set_block(uint32_t piece_id, uint32_t offset, bytes_span data) {
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
    const auto active_piece_length = piece_length(id);
    auto it = m_active_pieces.emplace(make_pair(
        id, make_shared<Piece>(PieceId(id), PieceSize(active_piece_length))));
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
  return std::ranges::all_of(m_active_pieces, [](const auto& piece) {
    return piece.second->piece_written();
  });
}

std::tuple<FileInfo, int64_t, int64_t> Torrent::file_at_pos(int64_t pos) const {
  if (is_single_file()) {
    return make_tuple(FileInfo(length(), tmpfile(), md5sum()), pos,
                      length() - pos);
  }
  int64_t cpos = 0;
  for (const auto& fi : files()) {
    if (pos < (cpos + fi.length())) {
      return make_tuple(fi, pos - cpos, cpos + fi.length() - pos);
    }
    cpos += fi.length();
  }
  throw runtime_error(fmt::format("pos > torrent size {}>{}", pos, length()));
}

void Torrent::last_piece_written() {
  m_logger->info("{} completed. Notifying peers and tracker.", m_name);

  for (auto& peer : m_peers) {
    if (!peer->is_listening()) {
      peer->set_am_interested(false);
    }
  }

  tracker_request(TrackerEvent::COMPLETED);
}

void Torrent::piece_done(std::shared_ptr<Piece>& piece) {
  m_client_pieces[piece->id()] = true;
  ranges::for_each(m_piece_callbacks, [&](const auto& cb) { cb(this, piece); });
}

ostream& operator<<(ostream& os, const zit::FileInfo& file_info) {
  os << "(" << file_info.path() << ", " << file_info.length() << " bytes";
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
  // Thread safety not critical here - just debug output
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
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
    os << "Length:        " << torrent.length() << " bytes ("
       << bytesToHumanReadable(torrent.length()) << ")\n";
    if (!torrent.md5sum().empty()) {
      os << "MD5Sum:        " << torrent.md5sum() << "\n";
    }
  } else {
    os << "Files:\n";
    for (const auto& fi : torrent.files()) {
      os << "               " << fi << "\n";
    }
  }
  os << "Announce:      " << torrent.announce() << "\n";
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
