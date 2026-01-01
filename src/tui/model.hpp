#pragma once

#include <asio/io_context.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
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
};

// TODO: move TorrentListModel out of the tui folder once zitlib integration lands.
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

  bool LaunchTorrent(const std::filesystem::path& path);
  void StopAllTorrents();

  std::vector<TorrentInfo> CollectSnapshot();
  void OnPolledSnapshot(std::vector<TorrentInfo> snapshot);
  void RegisterRareEventCallback(std::function<void(const TorrentInfo&)> cb);

 private:
  struct ActiveTorrent {
    std::filesystem::path source_path;
    std::unique_ptr<zit::Torrent> torrent;
    std::thread worker;
    size_t last_completed_pieces = 0;
    std::chrono::steady_clock::time_point last_snapshot =
        std::chrono::steady_clock::now();
  };

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
};

}  // namespace zit::tui
