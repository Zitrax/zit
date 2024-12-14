// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "bencode.hpp"
#include "global_config.hpp"
#include "peer.hpp"
#include "sha1.hpp"
#include "spdlog/fmt/ostr.h"  // Needed for spdlog to handle operator<<
#include "types.hpp"

namespace zit {

class Peer;
class Torrent;

using PieceCallback =
    std::function<void(Torrent*, const std::shared_ptr<Piece>&)>;

using DisconnectCallback = std::function<void(Peer*)>;

/**
 * Function taking an Url and returning the resulting Headers,Body of the
 * request. Matching Net::httpGet(const Url&).
 *
 * FIXME: Make bind_address something better than a plain string
 */
using HttpGet =
    std::function<std::tuple<std::string, std::string>(const Url&,
                                                       const std::string&)>;

/**
 * Information object for each file in the torrent.
 */
class FileInfo {
 public:
  FileInfo(int64_t length, std::filesystem::path path, std::string md5sum = "")
      : m_length(length),
        m_path(std::move(path)),
        m_md5sum(std::move(md5sum)) {}

  /**
   * Length of the file in bytes.
   */
  [[nodiscard]] auto length() const { return m_length; }

  /**
   * The path to the file.
   */
  [[nodiscard]] auto path() const { return m_path; }

  /**
   * A 32-character hexadecimal string corresponding to the MD5 sum of the
   * file. This is not used by BitTorrent at all, but it is included by some
   * programs for greater compatibility.
   */
  [[nodiscard]] auto md5sum() const { return m_md5sum; }

 private:
  int64_t m_length;
  std::filesystem::path m_path;
  std::string m_md5sum;
};

std::ostream& operator<<(std::ostream& os, const zit::FileInfo& file_info);

/**
 * Current each torrent has it's own run function. That results in each torrent
 * having to run in it's own thread. That seem unnecessary and would not really
 * scale.
 *
 * Also apparently new asio has deprecated io_service that is in current use,
 * io_context should be used instead.
 *
 * Probably what makes most sense is to provide an io_context from the outside
 * to the torrent, then it's up to the caller if it should be one global or
 * more. Something like: asio::io_context io; Torrent t1(io, ...); Torrent
 * t2(io, ...); io.run();
 *
 * However the torrent class currently performas some maintenance in it's run
 * function, how should that be handled if there is no run function and run is
 * called on the context from the outside?
 *
 * Could the maintenance functions could be scheduled instead of being rate
 * limited like now?
 */

/**
 * Represents one torrent. Bookeeps all pieces and block information.
 *
 * @note About locking; implemented as suggested in
 *  https://stackoverflow.com/a/14600868/11722
 *
 * Public function locks, private functions do not; this
 * should avoid problems with double locking and need
 * for reentrant mutexes.
 */
class Torrent {
 public:
  /**
   * @param file path to .torrent file to read
   * @param data_dir path to the directory with the downloaded result
   * @param config a config instance, by default reads config from disk
   * @param http_get a http request getter function
   */
  explicit Torrent(
      asio::io_context& io_context,
      std::filesystem::path file,
      std::filesystem::path data_dir = "",
      const Config& config = SingletonDirectoryFileConfig::getInstance(),
      HttpGet http_get = [](const Url& url, const std::string& bind_address) {
        return Net::httpGet(url, bind_address);
      });

  ~Torrent();

  /** The tracker URL */
  [[nodiscard]] auto announce() const { return m_announce; }

  /**
   * This key will refer to a list of lists of URLs, and will contain a list
   * of tiers of announces. If the client is compatible with the
   * multitracker specification, and if the "announce-list" key is present,
   * the client will ignore the "announce" key and only use the URLs in
   * "announce-list". (optional)
   */
  [[nodiscard]] auto announce_list() const { return m_announce_list; }

  /**
   * The creation time of the torrent, in standard UNIX epoch format
   * (integer, seconds since 1-Jan-1970 00:00:00 UTC) (optional)
   */
  [[nodiscard]] auto creation_date() const { return m_creation_date; }

  /**
   * Free-form textual comments of the author (optional)
   */
  [[nodiscard]] auto comment() const { return m_comment; }

  /**
   * Name and version of the program used to create the .torrent (optional)
   */
  [[nodiscard]] auto created_by() const { return m_created_by; }

  /**
   * The string encoding format used to generate the pieces part of the info
   * dictionary in the .torrent metafile. (optional)
   */
  [[nodiscard]] auto encoding() const { return m_encoding; }

  /**
   * Number of bytes in each piece.
   */
  [[nodiscard]] auto piece_length() const { return m_piece_length; }

  /**
   * Number of bytes in specific piece
   * Only since the last piece that might be shorter.
   */
  [[nodiscard]] uint32_t piece_length(uint32_t id) const;

  /**
   * Vector consisting of all 20-byte SHA1 hash values one per piece.
   */
  [[nodiscard]] auto pieces() const { return m_pieces; }

  /**
   * If true the client MUST publish its presence to get other peers ONLY
   * via the trackers explicitly described in the metainfo file. If false,
   * the client may obtain peer from other means, e.g. PEX peer exchange,
   * dht. Here, "private" may be read as "no external peer source".
   */
  [[nodiscard]] auto is_private() const { return m_private; }

  /**
   * The filename in single file mode or the in multiple file mode the
   * directory name. This is purely advisory.
   */
  [[nodiscard]] auto name() const { return m_name; }

  /**
   * In single file mode the length of the file in bytes, in multi file mode the
   * combined length of all files.
   */
  [[nodiscard]] int64_t length() const;

  /**
   * In single file mode the MD5 sum of the file. This is not used by
   * BitTorrent at all, but it is included by some programs for greater
   * compatibility.
   */
  [[nodiscard]] auto md5sum() const { return m_md5sum; }

  /**
   * In multi file mode this is the file information for each file.
   */
  [[nodiscard]] auto files() const { return m_files; }

  /**
   * If this is a single file torrent, there will be a 'length' but not 'files'.
   */
  [[nodiscard]] auto is_single_file() const { return m_length != 0; }

  /**
   * The 20 byte sha1 hash of the bencoded form of the info value from the
   * metainfo file. This value will almost certainly have to be escaped.
   *
   * Note that this is a substring of the metainfo file. The info-hash must be
   * the hash of the encoded form as found in the .torrent file, which is
   * identical to bdecoding the metainfo file, extracting the info dictionary
   * and encoding it if and only if the bdecoder fully validated the input (e.g.
   * key ordering, absence of leading zeros). Conversely that means clients must
   * either reject invalid metainfo files or extract the substring directly.
   * They must not perform a decode-encode roundtrip on invalid data.
   */
  [[nodiscard]] auto info_hash() const { return m_info_hash; }

  /**
   * Number of bytes downloaded
   */
  [[nodiscard]] auto downloaded() const;

  /**
   * Number of bytes left to download.
   */
  [[nodiscard]] auto left() const;

  /**
   * File on disk to which the torrent is written during transfer.
   * For a single torrent this is the actual file, for a multi torrent
   * this is a directory.
   */
  [[nodiscard]] auto tmpfile() const { return m_tmpfile; }

  /**
   * The .torrent file used to create this torrent.
   */
  [[nodiscard]] auto torrent_file() const { return m_torrent_file; }

  /**
   * The suffix of a file used during transfer (before completion)
   */
  [[nodiscard]] static auto tmpfileExtension() { return ".zit_downloading"; }

  /**
   * List of connected peers
   */
  [[nodiscard]] auto& peers() { return m_peers; }

  /**
   * Add peer if there is no other peer handling the same url.
   *
   * @param peer The peer to add
   * @param peers The vector to add the peer to, defaults to m_peers
   *
   * @return true if the peer was added
   */
  bool add_peer(
      std::shared_ptr<Peer> peer,
      std::optional<std::reference_wrapper<std::vector<std::shared_ptr<Peer>>>>
          peers = {});

  /**
   * Callback that will be called whenever a piece has finished downloading.
   */
  void add_piece_callback(PieceCallback piece_callback) {
    m_piece_callbacks.emplace_back(std::move(piece_callback));
  }

  /**
   * Callback when a peer disconnects.
   */
  void set_disconnect_callback(DisconnectCallback disconnect_callback) {
    m_disconnect_callback = std::move(disconnect_callback);
  }

  void disconnected(Peer* peer);

  /**
   * The first request to the tracker. After calling this the peer list
   * has been populated.
   */
  void start();

  /**
   * Will run this torrent until all peer connections have stopped.
   */
  void run();

  /**
   * Stopping all peers. Will cause the run() function to terminate.
   */
  void stop();

  /**
   * Peer received remote piece info, make sure we have a non empty client side
   * bitfield of the same size.
   *
   * m_client_pieces is also accessed by the file_writer, thus we need to lock.
   */
  void init_client_pieces(bytes::size_type count) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_client_pieces.size()) {
      m_client_pieces = Bitfield(count);
    }
  }

  /**
   * The pieces that we have (on disk).
   */
  [[nodiscard]] Bitfield client_pieces() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_client_pieces;
  }

  /**
   * Number of pieces on disk and in total.
   */
  [[nodiscard]] auto piece_status() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return std::make_pair(m_client_pieces.count(), m_pieces.size());
  }

  /**
   * Information about what pieces we don't have that a remote has.
   */
  [[nodiscard]] Bitfield relevant_pieces(const Bitfield& remote_pieces) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return remote_pieces - m_client_pieces;
  }

  /**
   * Store a retrieved block (part of a piece).
   *
   * @return true if the block was stored
   */
  bool set_block(uint32_t piece_id, uint32_t offset, bytes_span data);

  /**
   * Create or return active piece for specific id.
   *
   * @param id The id of the piece to get
   * @param create Creates the piece object if we do not already have it.
   */
  [[nodiscard]] std::shared_ptr<Piece> active_piece(uint32_t id,
                                                    bool create = true);

  /**
   * Return true if all pieces have been written to disk.
   */
  [[nodiscard]] bool done() const;

  /**
   * Called by FileWriter when it has written the last piece to disk.
   */
  void last_piece_written();

  /**
   * Port that we listen to.
   */
  [[nodiscard]] auto listening_port() const { return m_listening_port; }

  /**
   * Port for outgoing connections.
   */
  [[nodiscard]] auto connection_port() const { return m_connection_port; }

  /**
   * The peer_id used in the handshakes and tracker requests.
   */
  [[nodiscard]] auto peer_id() const { return m_peer_id; }

  /**
   * Read only configuration for this torrent.
   */
  [[nodiscard]] const auto& config() const { return m_config; }

  /**
   * Find the file at a specific position in the torrent.
   * @param pos The offset into the torrent file
   * @return A tuple with (FileInfo, int64_t offset_in_file, int64_t tail)
   */
  [[nodiscard]] std::tuple<FileInfo, int64_t, int64_t> file_at_pos(
      int64_t pos) const;

  enum class TrackerEvent { STARTED, STOPPED, COMPLETED, UNSPECIFIED };

  /**
   * Find a torrent with a specific info hash among all existing torrents.
   *
   * @param info_hash the info hash
   * @return Torrent* A torrent with the provided info hash or nullptr
   */
  static Torrent* get(const Sha1& info_hash);

  /**
   * The total number of torrents
   */
  static size_t count();

 private:
  /**
   * Read and add peers in string form and add to the peer vector.
   */
  void read_peers_string_list(const bencode::Element& peers_dict,
                              std::vector<std::shared_ptr<Peer>>& peers);

  /**
   * Read and add peers in binary form and add to the peer vector.
   */
  void read_peers_binary_form(const bencode::Element& peers_dict,
                              std::vector<std::shared_ptr<Peer>>& peers);

  /**
   * Helper for `verify_existing_file` for single file pieces.
   */
  void verify_piece_single_file(uintmax_t file_length,
                                std::atomic_uint32_t& num_pieces,
                                std::mutex& mutex,
                                const Sha1& sha1);

  /**
   * Helper for `verify_existing_file` for single multi file pieces.
   */
  void verify_piece_multi_file(std::atomic_uint32_t& num_pieces,
                               std::mutex& mutex,
                               int64_t global_len,
                               const Sha1& sha1);

  /**
   * If a piece have requests but have not received data in a while we mark the
   * blocks as non requested to be tried again.
   */
  void retry_pieces();

  /**
   * Go over the list of peers and remove inactive ones, and reconnect to new.
   */
  void retry_peers();

  friend std::ostream& operator<<(std::ostream& os, const TrackerEvent& te);

  friend std::string format_as(const Torrent::TrackerEvent& te);

  /**
   * Request a list of peers from the tracker.
   */
  std::vector<std::shared_ptr<Peer>> tracker_request(TrackerEvent event);

  /**
   * HTTP(S) tracker requests
   */
  std::pair<bool, std::vector<std::shared_ptr<Peer>>> http_tracker_request(
      const Url& announce_url,
      TrackerEvent event);

  /**
   * UDP tracker requests
   */
  std::pair<bool, std::vector<std::shared_ptr<Peer>>> udp_tracker_request(
      const Url& announce_url,
      TrackerEvent event);

  /**
   * Called when one piece has been downloaded.
   */
  void piece_done(std::shared_ptr<Piece>& piece);

  /**
   * Check an existing file for match with expected checksums.
   */
  void verify_existing_file();

  void schedule_retry_pieces();
  void schedule_retry_peers();

  asio::io_context& m_io_context;
  const Config& m_config;
  std::string m_announce{};
  std::vector<std::vector<std::string>> m_announce_list{};
  int64_t m_creation_date = 0;
  std::string m_comment{};
  std::string m_created_by{};
  std::string m_encoding{};
  uint32_t m_piece_length = 0;
  std::vector<Sha1> m_pieces{};
  bool m_private = false;
  std::string m_name{};
  int64_t m_length = 0;
  std::string m_md5sum{};
  std::vector<FileInfo> m_files{};
  Sha1 m_info_hash{};
  std::filesystem::path m_tmpfile{};
  std::filesystem::path m_data_dir{};
  std::filesystem::path m_torrent_file{};
  std::vector<PieceCallback> m_piece_callbacks{};
  DisconnectCallback m_disconnect_callback{};
  std::string m_peer_id{};
  ListeningPort m_listening_port;
  ConnectionPort m_connection_port;
  std::mutex m_peers_mutex{};
  std::vector<std::shared_ptr<Peer>> m_peers{};
  HttpGet m_http_get;
  asio::steady_timer m_retry_pieces_timer;
  asio::steady_timer m_retry_peers_timer;
  bool m_stopped = false;

  // Piece housekeeping
  mutable std::mutex m_mutex{};
  Bitfield m_client_pieces{};
  /** Piece id -> Piece object */
  std::map<uint32_t, std::shared_ptr<Piece>> m_active_pieces{};

  // This is ok and a bug in clang-tidy
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::mutex m_torrents_mutex;
  static std::map<Sha1, Torrent*> m_torrents;
};

std::ostream& operator<<(std::ostream& os, const zit::Torrent& torrent);

/** For fmt */
std::string format_as(const Torrent& torrent);

}  // namespace zit
