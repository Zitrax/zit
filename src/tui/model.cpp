#include "model.hpp"

#include <torrent.hpp>

#include <algorithm>
#include <utility>

namespace zit::tui {

TorrentListModel::TorrentListModel() {
  RefreshMenuEntries();
}

bool TorrentListModel::empty() const { return torrents_.empty(); }

const std::vector<TorrentInfo>& TorrentListModel::torrents() const {
  return torrents_;
}

std::vector<TorrentInfo>& TorrentListModel::torrents() { return torrents_; }

const std::vector<std::string>& TorrentListModel::menu_entries() const {
  return menu_entries_;
}

std::vector<std::string>& TorrentListModel::menu_entries() {
  return menu_entries_;
}

int TorrentListModel::selected_index() const { return selected_index_; }

int* TorrentListModel::mutable_selected_index() { return &selected_index_; }

const TorrentInfo* TorrentListModel::selected() const {
  if (torrents_.empty()) {
    return nullptr;
  }
  if (selected_index_ < 0 ||
      selected_index_ >= static_cast<int>(torrents_.size())) {
    return nullptr;
  }
  return &torrents_[static_cast<size_t>(selected_index_)];
}

void TorrentListModel::AddTorrent(TorrentInfo info) {
  torrents_.push_back(std::move(info));
  RefreshMenuEntries();
}

void TorrentListModel::Clear() {
  torrents_.clear();
  selected_index_ = 0;
  RefreshMenuEntries();
}

void TorrentListModel::OnPolledSnapshot(
    const std::vector<TorrentInfo>& snapshot) {
  torrents_ = snapshot;
  ClampSelection();
  RefreshMenuEntries();
}

void TorrentListModel::RegisterRareEventCallback(
    std::function<void(const TorrentInfo&)> cb) {
  rare_event_callback_ = std::move(cb);
}

void TorrentListModel::RefreshMenuEntries() {
  menu_entries_.clear();
  if (torrents_.empty()) {
    menu_entries_.emplace_back("No torrents. Press 'o' to add one.");
    selected_index_ = 0;
    return;
  }

  menu_entries_.reserve(torrents_.size());
  for (const auto& torrent : torrents_) {
    menu_entries_.push_back(torrent.name);
  }
  ClampSelection();
}

void TorrentListModel::ClampSelection() {
  if (menu_entries_.empty()) {
    selected_index_ = 0;
    return;
  }
  if (selected_index_ < 0) {
    selected_index_ = 0;
  }
  const int max_index = static_cast<int>(menu_entries_.size()) - 1;
  if (selected_index_ > max_index) {
    selected_index_ = max_index;
  }
}

}  // namespace zit::tui
