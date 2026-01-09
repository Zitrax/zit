/**
 * @file
 * @brief TUI model: owns torrent lifecycles, workers, and view-facing snapshots.
 * Exposes an immutable snapshot list and selection state for the UI. Threads:
 * creation worker (pending queue) plus per-torrent worker threads; synchronized
 * via mutexes/condition variables.
 */
#pragma once

#include <asio/io_context.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "file_writer.hpp"

namespace zit::tui {

struct TorrentInfo {
  std::string name;
  std::string complete;
  std::string size;
  std::string peers;
  std::string down_speed;
  std::string up_speed;
  std::string data_directory;

  // Torrent metadata (for detail view)
  std::string announce;
  std::string creation_date;
  std::string comment;
  std::string created_by;
  std::string encoding;
  std::string piece_count;
  std::string piece_length;
  std::string is_private;
  std::string info_hash;
  std::string files_info;

  // Piece data for visualization in UI (generated dynamically based on width)
  uint32_t pieces_completed = 0;
  uint32_t pieces_total = 0;

  // Per-piece completion status (true if piece is on disk)
  std::vector<bool> piece_completion;

  // Per-piece peer availability (count of peers that have each piece)
  std::vector<uint32_t> piece_peer_counts;
};

/// @note This "model" currently owns torrent orchestration (workers, creation queue)
/// in addition to exposing view-facing snapshots—an explicit MVC boundary tradeoff.
/// TODO: move TorrentListModel out of the tui folder once zitlib integration lands.
class TorrentListModel {
 public:
  TorrentListModel();
  ~TorrentListModel();

  bool empty() const;
  const std::vector<TorrentInfo>& torrents() const;
  std::vector<TorrentInfo>& torrents();

  const std::vector<std::string>& menu_entries() const;
  std::vector<std::string>& menu_entries();

  int selected_index() const;
  int* mutable_selected_index();
  const TorrentInfo* selected() const;

  bool LaunchTorrent(
      const std::filesystem::path& path,
      const std::filesystem::path& data_dir = std::filesystem::current_path());
  void StopAllTorrents();
  void StopTorrent(int index);
  std::vector<std::filesystem::path> GetTorrentPaths() const;
  std::vector<std::filesystem::path> GetDataDirectories() const;

  std::vector<TorrentInfo> CollectSnapshot();
  void OnPolledSnapshot(std::vector<TorrentInfo> snapshot);
  void RegisterRareEventCallback(std::function<void(const TorrentInfo&)> cb);

 private:
  struct ActiveTorrent {
    std::filesystem::path source_path;
    std::filesystem::path data_directory;
    std::unique_ptr<zit::Torrent> torrent;
    std::thread worker;
    size_t last_completed_pieces = 0;
    std::chrono::steady_clock::time_point last_snapshot =
        std::chrono::steady_clock::now();
  };

  struct PendingPlaceholder {
    std::filesystem::path source_path;
    std::filesystem::path data_directory;
    TorrentInfo info;
  };

  void StartCreationThread();
  void StopCreationThread();
  void CreationLoop();
  bool CreateAndStartTorrent(const std::filesystem::path& path,
                             const std::filesystem::path& data_dir);
  void RemovePlaceholderForPath(const std::filesystem::path& path);

  void RefreshMenuEntries();
  void ClampSelection();

  std::vector<TorrentInfo> torrents_;
  std::vector<std::string> menu_entries_;
  int selected_index_ = 0;
  std::function<void(const TorrentInfo&)> rare_event_callback_;

  asio::io_context m_io_context;
  std::unique_ptr<zit::FileWriterThread> file_writer_thread_;
  std::vector<std::unique_ptr<ActiveTorrent>> active_torrents_;
  mutable std::mutex active_mutex_;

  struct PendingRequest {
    std::filesystem::path torrent_path;
    std::filesystem::path data_directory;
  };
  std::queue<PendingRequest> pending_requests_;
  std::vector<PendingPlaceholder> pending_placeholders_;
  std::mutex pending_mutex_;
  std::condition_variable pending_cv_;
  std::atomic<bool> keep_creating_{true};
  std::thread creation_thread_;
};

}  // namespace zit::tui
