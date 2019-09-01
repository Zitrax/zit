// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "peer.h"
#include "sha1.h"

// Needed for spdlog to handle operator<<
#include "spdlog/fmt/ostr.h"

namespace zit {

class FileInfo;
class Peer;
class Torrent;

using PieceCallback =
    std::function<void(Torrent*, const std::shared_ptr<Piece>&)>;

using DisconnectCallback = std::function<void(Peer*)>;

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
   */
  explicit Torrent(const std::filesystem::path& file,
                   std::filesystem::path data_dir = "");

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
   * Number of bytes left to download.
   */
  [[nodiscard]] auto left() const;

  /**
   * File on disk to which the torrent is written during transfer
   */
  [[nodiscard]] auto tmpfile() const { return m_tmpfile; }

  /**
   * List of connected peers
   */
  [[nodiscard]] auto& peers() { return m_peers; }

  /**
   * Callback that will be called whenever a piece has finished downloading.
   */
  void set_piece_callback(PieceCallback piece_callback) {
    m_piece_callback = piece_callback;
  }

  /**
   * Callback when a peer disconnects.
   */
  void set_disconnect_callback(DisconnectCallback disconnect_callback) {
    m_disconnect_callback = disconnect_callback;
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
   * Peer received remote piece info, make sure we have a non empty client side
   * bitfield of the same size.
   *
   * m_client_pieces is also accessed by the file_writer, thus we need to lock.
   */
  void init_client_pieces(bytes::size_type count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_client_pieces.size()) {
      m_client_pieces = Bitfield(count);
    }
  }

  /**
   * The pieces that we have (on disk).
   */
  [[nodiscard]] Bitfield client_pieces() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_client_pieces;
  }

  /**
   * Information about what pieces we don't have that a remote has.
   */
  [[nodiscard]] Bitfield relevant_pieces(const Bitfield& remote_pieces) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return remote_pieces - m_client_pieces;
  }

  /**
   * Store a retrieved block (part of a piece).
   *
   * @return true if the block was stored
   */
  bool set_block(uint32_t piece_id, uint32_t offset, const bytes& data);

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
   * Port that we listen to.
   */
  [[nodiscard]] auto listning_port() const { return m_listening_port; }

  /**
   * Port for outgoing connections.
   */
  [[nodiscard]] auto connection_port() const { return m_connection_port; }

 private:
  /**
   * Called when one piece has been downloaded.
   */
  void piece_done(std::shared_ptr<Piece>& piece);

  /**
   * Check an existing file for match with expected checksums.
   */
  void verify_existing_file();

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
  std::shared_ptr<spdlog::logger> m_logger{};
  std::filesystem::path m_tmpfile{};
  std::filesystem::path m_data_dir{};
  PieceCallback m_piece_callback{};
  DisconnectCallback m_disconnect_callback{};
  // FIXME: Configurable ports
  unsigned short m_listening_port = 20001;
  unsigned short m_connection_port = 20000;
  std::vector<std::shared_ptr<Peer>> m_peers{};

  // Piece housekeeping
  mutable std::mutex m_mutex{};
  Bitfield m_client_pieces{};
  /** Piece id -> Piece object */
  std::map<uint32_t, std::shared_ptr<Piece>> m_active_pieces{};
};

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
std::ostream& operator<<(std::ostream& os, const zit::Torrent& torrent);

}  // namespace zit
