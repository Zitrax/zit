#pragma once

#include <asio/io_context.hpp>

#include <functional>
#include <string>
#include <vector>

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

  bool empty() const;
  const std::vector<TorrentInfo>& torrents() const;
  std::vector<TorrentInfo>& torrents();

  const std::vector<std::string>& menu_entries() const;
  std::vector<std::string>& menu_entries();

  int selected_index() const;
  int* mutable_selected_index();
  const TorrentInfo* selected() const;

  void AddTorrent(TorrentInfo info);
  void Clear();

  // Placeholder hooks for upcoming polling/callback plumbing.
  void OnPolledSnapshot(const std::vector<TorrentInfo>& snapshot);
  void RegisterRareEventCallback(std::function<void(const TorrentInfo&)> cb);

 private:
  void RefreshMenuEntries();
  void ClampSelection();

  std::vector<TorrentInfo> torrents_;
  std::vector<std::string> menu_entries_;
  int selected_index_ = 0;
  std::function<void(const TorrentInfo&)> rare_event_callback_;

  asio::io_context m_io_context;
};

}  // namespace zit::tui
