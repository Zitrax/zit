// -*- mode:c++; c-basic-offset : 2; -*-
#include "torrent.hpp"
#include "bencode.hpp"
#include "file_utils.hpp"
#include "global_config.hpp"
#include "logger.hpp"
#include "net.hpp"
#include "peer.hpp"
#include "piece.hpp"
#include "random.hpp"
#include "retry.hpp"
#include "scope_guard.hpp"
#include "sha1.hpp"
#include "string_utils.hpp"
#include "timer.hpp"
#include "types.hpp"
#include "version.hpp"

#include <fmt/format.h>
#include <spdlog/common.h>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>

#if __clang__
#include <bits/chrono.h>
#endif  // __clang__
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <execution>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef WIN32
#include <unistd.h>
#endif  // !WIN32

using namespace bencode;
using namespace std;

namespace std {
template <typename T>
// NOLINTNEXTLINE(cert-dcl58-cpp)
std::string format_as(const atomic<T>& t) {
  return std::to_string(t.load());
}
}  // namespace std

namespace zit {

namespace {

/**
 * To make transform calls more readable
 */
template <class In, class Out, class Op>
auto transform_all(const In& in, Out& out, Op func) {
  return transform(begin(in), end(in), back_inserter(out), func);
}

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
  const auto path_list =
      dict.at("path")->template to<TypedElement<BeList>>()->val();
  const filesystem::path path = std::accumulate(
      path_list.begin(), path_list.end(), filesystem::path{},
      [](const auto& a, const auto& b) {
        return a / b->template to<TypedElement<string>>()->val();
      });
  return FileInfo(
      dict.at("length")->template to<TypedElement<int64_t>>()->val(), path,
      md5);
}

}  // namespace

// This is ok and a bug in clang-tidy
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::map<Sha1, Torrent*> Torrent::m_torrents{};
mutex Torrent::m_torrents_mutex{};

// Could possibly be grouped in a struct
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Torrent::Torrent(asio::io_context& io_context,
                 filesystem::path file,
                 std::filesystem::path data_dir,
                 const Config& config,
                 HttpGet http_get)
    : m_io_context(io_context),
      m_config(config),
      m_data_dir(std::move(data_dir)),
      m_torrent_file(std::move(file)),
      // Peer-ID Azureus style
      m_peer_id(fmt::format("-ZI{:02}{:02}-{}",
                            MAJOR_VERSION,
                            MINOR_VERSION,
                            random_string(12))),
      m_listening_port(
          numeric_cast<unsigned short>(config.get(IntSetting::LISTENING_PORT),
                                       "listening port out of range")),
      m_connection_port(
          numeric_cast<unsigned short>(config.get(IntSetting::CONNECTION_PORT),
                                       "connection port out of range")),
      m_http_get(std::move(http_get)),
      m_retry_pieces_timer(m_io_context),
      m_retry_peers_timer(m_io_context) {
  auto root = bencode::decode(read_file(m_torrent_file));

  const auto& root_dict = root->to<TypedElement<BeDict>>()->val();

  auto has = [&root_dict](const auto& key) {
    return root_dict.find(key) != root_dict.end();
  };

  // Required
  if (!has("announce")) {
    throw runtime_error(
        "Zit does not currently support torrents without the announce field");
  }
  m_announce = root_dict.at("announce")->to<TypedElement<string>>()->val();
  const auto& info = root_dict.at("info")->to<TypedElement<BeDict>>()->val();

  m_name = (m_data_dir / info.at("name")->to<TypedElement<string>>()->val())
               .string();
  m_tmpfile = m_name + Torrent::tmpfileExtension();
  logger()->debug("Using tmpfile {} for {}", m_tmpfile, m_torrent_file);
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
      return std::move(tier_output_list);
    });
  }

  m_info_hash = Sha1::calculateData(encode(info));

  // If we already have a file - scan it and mark what pieces we already have
  verify_existing_file();

  {
    scoped_lock lock(m_torrents_mutex);
    if (m_torrents.contains(m_info_hash)) {
      throw runtime_error("Torrent already exists");
    }

    m_torrents.insert({m_info_hash, this});
  }
}

Torrent::~Torrent() {
  scoped_lock lock(m_torrents_mutex);
  m_torrents.erase(m_info_hash);
}

uint32_t Torrent::piece_length(uint32_t id) const {
  if (id == pieces().size() - 1) {
    // The last piece might be shorter
    const auto mod = numeric_cast<uint32_t>(length() % m_piece_length);
    return mod ? mod : m_piece_length;
  }
  return m_piece_length;
}

void Torrent::verify_piece_single_file(uintmax_t file_length,
                                       std::atomic_uint32_t& num_pieces,
                                       std::mutex& mutex,
                                       const Sha1& sha1) {
  const auto id = zit::numeric_cast<uint32_t>(&sha1 - m_pieces.data());
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
    const std::lock_guard<std::mutex> lock(mutex);
    m_client_pieces[id] = true;
    m_active_pieces.emplace(
        id, make_shared<Piece>(PieceId(id), PieceSize(piece_length(id))));
    m_active_pieces[id]->set_piece_written(true);
    ++num_pieces;
  } else {
    logger()->trace("Piece {} does not match ({}!={})", id, sha1, fsha1);
  }
}

void Torrent::verify_piece_multi_file(std::atomic_uint32_t& num_pieces,
                                      std::mutex& mutex,
                                      int64_t global_len,
                                      const Sha1& sha1) {
  const auto id = zit::numeric_cast<uint32_t>(&sha1 - m_pieces.data());
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
    is.read(reinterpret_cast<char*>(
                std::next(data.data(), numeric_cast<std::ptrdiff_t>(ppos))),
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
    const std::lock_guard<std::mutex> lock(mutex);
    m_client_pieces[id] = true;
    m_active_pieces.emplace(
        id, make_shared<Piece>(PieceId(id), PieceSize(piece_length(id))));
    m_active_pieces[id]->set_piece_written(true);
    ++num_pieces;
  } else {
    logger()->trace("Piece {} does not match ({}!={})", id, sha1, fsha1);
  }
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
    const bool use_threads = m_config.get(BoolSetting::PIECE_VERIFY_THREADS);

    const auto verify = [this, &use_threads](auto&& verify_piece) {
      // Verify each piece in parallel to speed it up
      if (use_threads) {
        std::for_each(std::execution::par_unseq, m_pieces.begin(),
                      m_pieces.end(), verify_piece);
      } else {
        std::for_each(std::execution::unseq, m_pieces.begin(), m_pieces.end(),
                      verify_piece);
      }
    };

    {
      const Timer timer(
          fmt::format("verifying existing file(s) ({}using threads)",
                      use_threads ? "" : "not "));

      if (is_single_file()) {
        logger()->info("Verifying existing file: {}", m_tmpfile);
        const auto file_length = filesystem::file_size(m_tmpfile);

        verify([&file_length, &num_pieces, &mutex,
                this  // Be careful what is used from this. It
                      // needs to be thread safe.
        ](const Sha1& sha1) {
          verify_piece_single_file(file_length, num_pieces, mutex, sha1);
        });

      } else {
        logger()->info("Verifying existing files in: {}", m_tmpfile);
        const auto global_len = length();

        verify([&num_pieces, &mutex, &global_len,
                this  // Be careful what is used from this. It
                      // needs to be thread safe.
        ](const Sha1& sha1) {
          verify_piece_multi_file(num_pieces, mutex, global_len, sha1);
        });
      }
    }
    logger()->info("Verification done. {}/{} pieces done.", num_pieces,
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

std::string format_as(const Torrent::TrackerEvent& te) {
  std::stringstream ss;
  ss << te;
  return ss.str();
}

namespace {

bool is_local(const auto& purl, auto port) {
  return purl.host() == "127.0.0.1" && purl.port() == port.get();
}

}  // namespace

std::pair<bool, std::vector<std::shared_ptr<Peer>>>
Torrent::http_tracker_request(const Url& announce_url, TrackerEvent event) {
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
  // Was originally sent with an empty string - but it seem that some trackers
  // do not accept that, so skip it completely for unspecified.
  if (event != TrackerEvent::UNSPECIFIED) {
    url.add_param("event=" + fmt::format("{}", event));
  }
  url.add_param("compact=1");

  if (logger()->should_log(spdlog::level::debug)) {
    logger()->debug("HTTP Tracker request ({}):\n{}", event, url);
  } else {
    logger()->info("HTTP Tracker request ({}): {}", event, url.str());
  }

  auto [headers, body] =
      m_http_get(url, Net::BindAddress(m_config.get(StringSetting::BIND_ADDRESS)));

  std::vector<std::shared_ptr<Peer>> peers;

  // We only care about decoding the peer list for certain events
  if (event == TrackerEvent::UNSPECIFIED || event == TrackerEvent::STARTED) {
    if (!m_config.get(BoolSetting::INITIATE_PEER_CONNECTIONS) && done()) {
      logger()->debug("Skipping peer list since the torrent is completed.");
      return {true, {}};
    }

    ElmPtr reply;
    try {
      reply = decode(body);
    } catch (const BencodeConversionError&) {
      throw_with_nested(runtime_error("Could not decode peer list."));
    }

    logger()->debug("=====HEADER=====\n{}\n=====BODY=====\n{}", headers, reply);

    auto reply_dict = reply->to<TypedElement<BeDict>>()->val();
    if (reply_dict.find("failure reason") != reply_dict.end()) {
      throw runtime_error(
          "Tracker request failed: " +
          reply_dict["failure reason"]->to<TypedElement<string>>()->val());
    }

    if (reply_dict.find("peers") == reply_dict.end()) {
      throw runtime_error("Invalid tracker reply, no peer list");
    }
    auto peers_dict = reply_dict["peers"];
    // The peers might be in binary or string form
    // First try string form ...
    if (peers_dict->is<TypedElement<BeList>>()) {
      logger()->debug("Peer list in string form");
      read_peers_string_list(*peers_dict, peers);
    } else {
      // ... else compact/binary form
      logger()->debug("Peer list in binary form");
      read_peers_binary_form(*peers_dict, peers);
    }
  }
  return {true, peers};
}

/**
 * Protocol documented here: https://libtorrent.org/udp_tracker_protocol.html
 *
 * The requests are currently retired and synchronous so can be slow
 * if all requests fail. A possible improvement would be to look up
 * multiple trackers at the same time with async connections.
 */
class UDPTrackerRequest {
  using ClockType = std::chrono::high_resolution_clock;
  using TimePoint = std::chrono::time_point<ClockType>;

  template <std::integral T, typename Container>
  void append_big_endian(T val, Container& container) {
    ranges::move(to_big_endian<T>(val), std::back_inserter(container));
  }

  template <typename Container>
  void append_big_endian(bytes&& data, Container& container) {
    ranges::move(std::move(data), std::back_inserter(container));
  }

 public:
  UDPTrackerRequest(Url announce_url, Torrent& torrent)
      : m_announce_url(std::move(announce_url)), m_torrent(torrent) {}

  /**
   * Announce request to get peer list
   */
  std::pair<bool, std::vector<std::shared_ptr<Peer>>> announce(
      Torrent::TrackerEvent event) {
    if (logger()->should_log(spdlog::level::debug)) {
      logger()->debug("UDP Tracker request ({}):\n{}", event, m_announce_url);
    } else {
      logger()->info("UDP Tracker request ({}): {}", event,
                     m_announce_url.str());
    }

    if (!connect()) {
      return {};
    }

    if (!m_connection_id) {
      throw std::runtime_error(
          "UDPTrackerRequest::announce called without a connection");
    }

    const auto transaction_id = random_value<int32_t>();

    bytes announce_request;
    append_big_endian(m_connection_id.value(), announce_request);
    append_big_endian(static_cast<int32_t>(UdpAction::ANNOUNCE),
                      announce_request);
    append_big_endian(transaction_id, announce_request);
    append_big_endian(m_torrent.info_hash().bytes(), announce_request);
    append_big_endian(to_bytes(m_torrent.peer_id()), announce_request);
    append_big_endian(m_torrent.downloaded(), announce_request);
    append_big_endian(m_torrent.left(), announce_request);
    // FIXME: This is the upload bytes count - not yet keeping track of that
    constexpr int64_t uploaded_bytes{0};
    append_big_endian(uploaded_bytes, announce_request);
    append_big_endian(static_cast<int32_t>(event), announce_request);
    // Using 0 to indicate the sender IP (but could be an explicit IP)
    constexpr uint32_t my_ip{0};
    append_big_endian(my_ip, announce_request);
    const auto key = random_value<uint32_t>();
    append_big_endian(key, announce_request);
    constexpr int32_t max_peers_wanted{50};
    append_big_endian(max_peers_wanted, announce_request);
    append_big_endian(m_torrent.listening_port().get(), announce_request);
    // FIXME: This is related to authentication. Not supported at the moment.
    constexpr uint16_t extensions{0};
    append_big_endian(extensions, announce_request);

    assert(announce_request.size() == 100);

    const auto maybe_announce_response = retry_call(
        [&]() -> std::optional<bytes> {
          auto response = Net::udpRequest(m_announce_url, announce_request, 3s);
          if (response.empty()) {
            return {};
          }
          return response;
        },
        2, 3s);
    if (!maybe_announce_response) {
      logger()->debug("UDP Tracker request: empty announce response");
      return {true, {}};
    }
    const auto& announce_response = maybe_announce_response.value();

    const auto announce_reply_action =
        toUdpAction(from_big_endian<int32_t>(announce_response));
    const auto announce_reply_transaction_id =
        from_big_endian<int32_t>(announce_response, sizeof(int32_t));

    if (announce_reply_transaction_id != transaction_id) {
      logger()->warn("Udp request got unexpected transaction id {} != {}",
                     announce_reply_transaction_id, transaction_id);
      return {false, {}};
    }

    switch (announce_reply_action) {
      case UdpAction::ANNOUNCE:
        logger()->debug("UDP Tracker request announce");
        break;
      case UdpAction::ERR: {
        std::stringstream error_msg;
        std::transform(announce_response.cbegin() + 2 * sizeof(int32_t),
                       announce_response.cend(),
                       std::ostream_iterator<char>(error_msg),
                       [](std::byte b) { return static_cast<char>(b); });
        logger()->warn("UDP Tracker request error: {}", error_msg.str());
        return {false, {}};
      }
      case UdpAction::CONNECT:
      case UdpAction::SCRAPE:
        logger()->warn("UDP Tracker request unexpected action: {}",
                       static_cast<int>(announce_reply_action));
        return {false, {}};
    }

    if (event == Torrent::TrackerEvent::UNSPECIFIED ||
        event == Torrent::TrackerEvent::STARTED) {
      if (!m_torrent.config().get(BoolSetting::INITIATE_PEER_CONNECTIONS) &&
          m_torrent.done()) {
        logger()->debug("Skipping peer list since the torrent is completed.");
        return {true, {}};
      }

      // TODO: Number of seconds to wait before re-annoncing
      const auto interval =
          from_big_endian<int32_t>(announce_response, 2 * sizeof(int32_t));
      const auto leechers =
          from_big_endian<int32_t>(announce_response, 3 * sizeof(int32_t));
      const auto seeders =
          from_big_endian<int32_t>(announce_response, 4 * sizeof(int32_t));
      logger()->debug("interval: {} leechers: {} seeders: {}", interval,
                      leechers, seeders);
      std::vector<std::shared_ptr<Peer>> peers;
      constexpr auto size_of_peer = sizeof(uint16_t) + sizeof(int32_t);
      constexpr auto peer_offset = 5 * sizeof(int32_t);
      const auto peers_in_list =
          (announce_response.size() - peer_offset) / size_of_peer;
      logger()->debug("Parsing {} peers", peers_in_list);
      for (size_t i = 0; i < peers_in_list; ++i) {
        const auto ip = from_big_endian<int32_t>(
            announce_response, peer_offset + i * size_of_peer);
        const auto port = from_big_endian<uint16_t>(
            announce_response,
            peer_offset + i * size_of_peer + sizeof(int32_t));

        const auto ip_str =
            fmt::format("{}.{}.{}.{}", static_cast<uint8_t>((ip >> 24) & 0xFF),
                        static_cast<uint8_t>((ip >> 16) & 0xFF),
                        static_cast<uint8_t>((ip >> 8) & 0xFF),
                        static_cast<uint8_t>((ip >> 0) & 0xFF));

        const Url purl{
            fmt::format("http://{}:{}", ip_str, port), Url::Binary{false},
            Url::Resolve{m_torrent.config().get(BoolSetting::RESOLVE_URLS)}};
        if (!is_local(purl, m_torrent.listening_port())) {
          peers.emplace_back(std::make_shared<Peer>(purl, m_torrent));
        }
      }

      return {true, peers};
    }
    return {true, {}};
  }

 private:
  enum class UdpAction { CONNECT = 0, ANNOUNCE = 1, SCRAPE = 2, ERR = 3 };

  /** Connect to UDP tracker */
  bool connect() {
    if ((ClockType::now() - m_last_connection_id) > 1min) {
      m_connection_id.reset();
    } else if (m_connection_id) {
      logger()->trace("UDPTrackerRequest already connected");
      return true;
    }

    constexpr int64_t connection_id{0x41727101980};
    auto transaction_id = random_value<int32_t>();

    bytes connect_request;
    append_big_endian(connection_id, connect_request);
    append_big_endian(static_cast<int32_t>(UdpAction::CONNECT),
                      connect_request);
    append_big_endian(transaction_id, connect_request);

    const auto maybe_connect_response = retry_call(
        [&]() -> std::optional<bytes> {
          auto response = Net::udpRequest(m_announce_url, connect_request, 3s);
          if (response.empty()) {
            return {};
          }
          return response;
        },
        2, 3s);

    if (!maybe_connect_response) {
      logger()->debug("UDP Tracker request: empty connect response");
      return false;
    }

    const auto& connect_response = maybe_connect_response.value();

    if (connect_response.size() < 16) {
      logger()->debug("UDP Tracker request: too short connect response");
      return false;
    }

    const auto connect_reply_action =
        toUdpAction(from_big_endian<int32_t>(connect_response));
    const auto connect_reply_transaction_id =
        from_big_endian<int32_t>(connect_response, sizeof(int32_t));

    if (connect_reply_transaction_id != transaction_id) {
      logger()->warn("Udp request got unexpected transaction id {} != {}",
                     connect_reply_transaction_id, transaction_id);
      return false;
    }

    switch (connect_reply_action) {
      case UdpAction::CONNECT:
        logger()->debug("UDP Tracker request connected");
        break;
      case UdpAction::ERR: {
        std::stringstream error_msg;
        std::transform(connect_response.cbegin() + 2 * sizeof(int32_t),
                       connect_response.cend(),
                       std::ostream_iterator<char>(error_msg),
                       [](std::byte b) { return static_cast<char>(b); });
        logger()->warn("UDP Tracker request error: {}", error_msg.str());
        return false;
      }
      case UdpAction::ANNOUNCE:
      case UdpAction::SCRAPE:
        logger()->warn("UDP Tracker request unexpected action: {}",
                       static_cast<int>(connect_reply_action));
        return false;
    }

    m_connection_id =
        from_big_endian<int64_t>(connect_response, 2 * sizeof(int32_t));
    m_last_connection_id = ClockType::now();
    return true;
  }

  static UdpAction toUdpAction(int32_t action) {
    switch (action) {
      case static_cast<uint32_t>(UdpAction::CONNECT):
        return UdpAction::CONNECT;
      case static_cast<uint32_t>(UdpAction::ANNOUNCE):
        return UdpAction::ANNOUNCE;
      case static_cast<uint32_t>(UdpAction::SCRAPE):
        return UdpAction::SCRAPE;
      case static_cast<uint32_t>(UdpAction::ERR):
        return UdpAction::ERR;
      default:
        throw std::runtime_error("Unknown udp tracker action: " +
                                 std::to_string(action));
    }
  }

  Url m_announce_url;
  Torrent& m_torrent;
  std::optional<int64_t> m_connection_id{};
  TimePoint m_last_connection_id{ClockType::time_point::min()};
};

std::pair<bool, std::vector<std::shared_ptr<Peer>>>
Torrent::udp_tracker_request(const Url& announce_url, TrackerEvent event) {
  // FIXME: Replace this with a max size map, that throws out older request
  //        objects to keep a fixed size max.
  static std::map<Url, UDPTrackerRequest> tracker_requests;
  tracker_requests.try_emplace(announce_url, announce_url, *this);
  return tracker_requests.at(announce_url).announce(event);
}

std::vector<std::shared_ptr<Peer>> Torrent::tracker_request(
    TrackerEvent event) {
  // Following multi-tracker spec:
  // https://github.com/rakshasa/libtorrent/blob/master/doc/multitracker-spec.txt
  const auto local_announce_list =
      [this]() -> std::vector<std::vector<std::string>> {
    if (!m_announce_list.empty()) {
      return m_announce_list;
    }
    return {{m_announce}};
  }();

  auto do_tracker_request = [&](const auto& announce_url)
      -> std::pair<bool, std::vector<std::shared_ptr<Peer>>> {
    const Url url(announce_url);
    if (url.scheme().starts_with("http")) {
      return http_tracker_request(url, event);
    }

    if (url.scheme() == "udp") {
      return udp_tracker_request(url, event);
    }

    throw std::runtime_error(
        fmt::format("Unhandled tracker url scheme: {}", url.scheme()));
  };

  // According to the spec we will now try each tier in order and within a tier
  // we shuffle the announce list.

  std::exception_ptr thrown;
  std::vector<std::shared_ptr<Peer>> peers_from_tracker;
  bool success = false;

  std::random_device rd;
  std::mt19937 g(rd());
  for (auto tier : local_announce_list) {
    std::ranges::shuffle(tier, g);
    for (const auto& announce_url : tier) {
      try {
        if (m_stopped && event != TrackerEvent::STOPPED) {
          break;
        }
        std::tie(success, peers_from_tracker) =
            do_tracker_request(announce_url);
        thrown = nullptr;
        break;
      } catch (const HttpException& ex) {
        // Non-fatal issue, just ignore this tracker and move on
        logger()->debug("tracker_request: {}: {}", announce_url, ex.what());
      } catch (const std::exception& ex) {
        logger()->warn("tracker_request: {}: {}", announce_url, ex.what());
        thrown = std::current_exception();
      }
    }
    if (success) {
      break;
    }
  }

  if (thrown) {
    std::rethrow_exception(thrown);
  }

  // To avoid connecting to our own listening peer
  const auto is_local_peer = [&](const auto& peer) {
    const auto& url = peer->url();
    static const auto local_ips = [] {
      auto resolved = get_host_ip_addresses();
      resolved.emplace_back("localhost");
      static auto docker_ip{"172.17.0.1"};
      resolved.emplace_back(docker_ip);
      return std::move(resolved);
    }();
    return url && ranges::contains(local_ips, url->host()) &&
           url->port().value_or(0) == m_listening_port.get();
  };

  peers_from_tracker.erase(
      ranges::begin(ranges::remove_if(peers_from_tracker, is_local_peer)),
      ranges::end(peers_from_tracker));

  return peers_from_tracker;
}

void Torrent::start() {
  scoped_lock lock(m_peers_mutex);
  if (!m_peers.empty()) {
    throw runtime_error("Local peer vector not empty");
  }

  // Fetch a list of peers from the tracker
  m_peers = tracker_request(TrackerEvent::STARTED);

  // Handshake with all the remote peers
  logger()->info("Starting handshake with {} peers", m_peers.size());
  for (auto& p : m_peers) {
    p->handshake();
  }

  PeerAcceptor::acceptOnPort(m_io_context, m_listening_port,
                             m_config.get(StringSetting::BIND_ADDRESS));

  // Schedule maintenance functions
  schedule_retry_pieces();
  schedule_retry_peers();
}

void Torrent::schedule_retry_pieces() {
  const auto interval_seconds =
      m_config.get(IntSetting::RETRY_PIECES_INTERVAL_SECONDS);
  m_retry_pieces_timer.expires_after(std::chrono::seconds(interval_seconds));
  logger()->debug("Scheduling next retry_pieces in {}s", interval_seconds);
  m_retry_pieces_timer.async_wait([this](const asio::error_code& ec) {
    if (!ec) {
      retry_pieces();
    } else {
      logger()->warn("Timer error: {}", ec.message());
    }
  });
}

void Torrent::schedule_retry_peers() {
  const auto interval_seconds =
      m_config.get(IntSetting::RETRY_PEERS_INTERVAL_SECONDS);
  m_retry_peers_timer.expires_after(std::chrono::seconds(interval_seconds));
  logger()->debug("Scheduling next retry_peers in {}s", interval_seconds);
  m_retry_peers_timer.async_wait([this](const asio::error_code& ec) {
    if (!ec) {
      retry_peers();
    } else {
      logger()->warn("Timer error: {}", ec.message());
    }
  });
}

// FIXME: Remove this function
void Torrent::run() {
  logger()->debug("Run loop start");
  const auto is_stopped = [](auto& p) { return p->io_service().stopped(); };
  while (!m_stopped && (!done() || !ranges::all_of(m_peers, is_stopped))) {
    std::size_t ran = 0;
    {
      scoped_lock lock(m_peers_mutex);
      for (auto& p : m_peers) {
        ran += p->io_service().poll_one();
      }
    }
    ran += m_io_context.poll_one();
    //  If no handlers ran, then sleep.
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
  logger()->debug("Run loop done");
}

void Torrent::stop() {
  scoped_lock lock(m_peers_mutex);
  for (auto& peer : m_peers) {
    peer->stop();
  }
  m_stopped = true;
  tracker_request(TrackerEvent::STOPPED);
}

Torrent* Torrent::get(const Sha1& info_hash) {
  scoped_lock lock(m_torrents_mutex);
  if (auto match = m_torrents.find(info_hash); match != m_torrents.end()) {
    return match->second;
  }
  return nullptr;
}

size_t Torrent::count() {
  scoped_lock lock(m_torrents_mutex);
  return m_torrents.size();
}

void Torrent::read_peers_string_list(
    const bencode::Element& peers_dict,
    std::vector<std::shared_ptr<Peer>>& peers) {
  const auto peer_list = peers_dict.to<TypedElement<BeList>>()->val();
  for (const auto& elm : peer_list) {
    const auto peer = elm->to<TypedElement<BeDict>>()->val();
    const auto purl =
        Url(fmt::format("http://{}:{}",
                        peer.at("ip")->to<TypedElement<std::string>>()->val(),
                        peer.at("port")->to<TypedElement<int64_t>>()->val()),
            Url::Binary{false},
            Url::Resolve{m_config.get(BoolSetting::RESOLVE_URLS)});
    if (purl.is_ipv6()) {
      logger()->trace("Skipping IPv6 peer: {}", purl.str());
      continue;
    }
    if (!is_local(purl, listening_port())) {
      peers.emplace_back(make_shared<Peer>(purl, *this));
    }
  }
}

void Torrent::read_peers_binary_form(
    const bencode::Element& peers_dict,
    std::vector<std::shared_ptr<Peer>>& peers) {
  auto binary_peers = peers_dict.to<TypedElement<string>>()->val();
  if (binary_peers.empty()) {
    throw runtime_error("Peer list is empty");
  }

  const size_t BINARY_PEER_LENGTH{6};
  for (unsigned long i = 0; i < binary_peers.length();
       i += BINARY_PEER_LENGTH) {
    const auto purl =
        Url(binary_peers.substr(i, BINARY_PEER_LENGTH), Url::Binary{true},
            Url::Resolve{m_config.get(BoolSetting::RESOLVE_URLS)});
    if (purl.is_ipv6()) {
      logger()->trace("Skipping IPv6 peer: {}", purl.str());
      continue;
    }
    if (!is_local(purl, listening_port())) {
      peers.emplace_back(make_shared<Peer>(purl, *this));
    }
  }
}

void Torrent::retry_pieces() {
  if (m_stopped) {
    return;
  }

  const ScopeGuard scope_guard([this]() { schedule_retry_pieces(); });

  logger()->debug("Checking pieces for retry");
  std::size_t retry = 0;
  for (auto& [id, piece] : m_active_pieces) {
    retry += piece->retry_blocks();
  }
  logger()->trace("retry count = {}", retry);
  if (!retry) {
    return;
  }
  logger()->info("Marked {} blocks for retry", retry);

  // To hit different peers for each invocation - shuffle the list
  std::random_device rd;
  std::mt19937 g(rd());
  scoped_lock lock(m_peers_mutex);
  shuffle(m_peers.begin(), m_peers.end(), g);

  auto it = m_peers.begin();
  if (it == m_peers.end()) {
    logger()->warn("No peers available for retrying");
    return;
  }
  auto start_count = retry;
  while (retry > 0) {
    retry -= (*it)->request_next_block(1);
    it++;
    if (it == m_peers.end()) {
      if (retry == start_count) {
        logger()->warn("Could not retry all blocks.");
        break;
      }
      start_count = retry;
      it = m_peers.begin();
    }
  }
}

bool Torrent::add_peer(
    shared_ptr<Peer> peer,
    optional<reference_wrapper<std::vector<std::shared_ptr<Peer>>>> peers) {
  const bool in_use =
      std::find_if(m_peers.begin(), m_peers.end(),
                   [&peer](auto& existing_peer) {
                     const auto& lurl = peer->url();
                     const auto& rurl = existing_peer->url();
                     return lurl.has_value() && rurl.has_value() &&
                            peer->url().value().str() ==
                                existing_peer->url().value().str();
                   }) != m_peers.end();
  logger()->debug("Candidate {} in use: {}", peer->str(), in_use);
  if (!in_use) {
    peer->handshake();
    if (peers) {
      peers->get().push_back(peer);
    } else {
      m_peers.push_back(peer);
    }
    return true;
  }
  return false;
}

void Torrent::retry_peers() {
  if (m_stopped) {
    return;
  }

  scoped_lock lock(m_peers_mutex);
  const ScopeGuard scope_guard([this]() { schedule_retry_peers(); });

  logger()->debug("Checking peers for retry");

  // Find and disconnect inactive peers
  const auto [it_active, it_end] = ranges::partition(
      m_peers, [](auto& p) { return p->is_inactive() && !p->is_listening(); });

  const auto inactive = std::distance(m_peers.begin(), it_active);

  if (inactive) {
    logger()->info("Stopping {} inactive peers", inactive);
    std::for_each(it_active, it_end, [](auto& p) { p->stop(); });
  }

  // Connect to new peers (discarding the ones we just dropped or active ones)
  const auto tracker_peers = tracker_request(TrackerEvent::UNSPECIFIED);
  logger()->debug("{} candidate peers", tracker_peers.size());
  std::vector<std::shared_ptr<Peer>> new_peers;

  for (const auto& tracker_peer : tracker_peers) {
    add_peer(tracker_peer, new_peers);
  }

  if (!new_peers.empty()) {
    logger()->info("Found {} new peers", new_peers.size());
  }

  if (inactive) {
    m_peers.erase(m_peers.begin(), it_active);
  }

  m_peers.insert(m_peers.end(), new_peers.begin(), new_peers.end());
}

// TODO: Yes, should group these in one type
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool Torrent::set_block(uint32_t piece_id, uint32_t offset, bytes_span data) {
  const std::lock_guard<std::mutex> lock(m_mutex);
  // Look up relevant piece object among active pieces
  if (m_active_pieces.find(piece_id) != m_active_pieces.end()) {
    auto piece = m_active_pieces[piece_id];
    if (piece->set_block(offset, data)) {
      logger()->debug("Piece {} done!", piece_id);
      piece_done(piece);
    }
    return true;
  }
  logger()->warn("Tried to set block for non active piece");
  return false;
}

std::shared_ptr<Piece> Torrent::active_piece(uint32_t id, bool create) {
  const std::lock_guard<std::mutex> lock(m_mutex);
  auto piece = m_active_pieces.find(id);
  if (piece == m_active_pieces.end()) {
    if (!create) {
      return nullptr;
    }
    const auto active_piece_length = piece_length(id);
    auto it = m_active_pieces.emplace(
        id, make_shared<Piece>(PieceId(id), PieceSize(active_piece_length)));
    return it.first->second;
  }
  return m_active_pieces.at(id);
}

bool Torrent::done() const {
  const std::lock_guard<std::mutex> lock(m_mutex);

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
  logger()->info("{} completed. Notifying peers and tracker.", m_name);

  {
    scoped_lock lock(m_peers_mutex);
    for (auto& peer : m_peers) {
      if (!peer->is_listening()) {
        peer->set_am_interested(false);
      }
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
  os << "Info hash:     " << torrent.info_hash() << "\n";
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

std::string format_as(const Torrent& torrent) {
  std::stringstream ss;
  ss << torrent;
  return ss.str();
}

}  // namespace zit
