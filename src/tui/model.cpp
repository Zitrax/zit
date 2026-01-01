#include "model.hpp"

#include <torrent.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <thread>
#include <utility>

#include "file_writer.hpp"
#include "logger.hpp"

namespace zit::tui {

namespace {

using Clock = std::chrono::steady_clock;

std::string FormatBytes(int64_t bytes) {
  static constexpr std::array<const char*, 5> kSuffixes{"B", "KiB", "MiB",
                                                        "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  size_t idx = 0;
  while (value >= 1024.0 && idx + 1 < kSuffixes.size()) {
    value /= 1024.0;
    ++idx;
  }
  return fmt::format("{:.1f} {}", value, kSuffixes[idx]);
}

std::string FormatRate(double bytes_per_second) {
  if (bytes_per_second <= 0.0) {
    return "0 B/s";
  }
  static constexpr std::array<const char*, 4> kSuffixes{"B/s", "KiB/s", "MiB/s",
                                                        "GiB/s"};
  double value = bytes_per_second;
  size_t idx = 0;
  while (value >= 1024.0 && idx + 1 < kSuffixes.size()) {
    value /= 1024.0;
    ++idx;
  }
  return fmt::format("{:.1f} {}", value, kSuffixes[idx]);
}

void LogException(const std::exception& ex) {
  zit::logger()->error("Torrent thread error: {}", ex.what());
  try {
    std::rethrow_if_nested(ex);
  } catch (const std::exception& nested) {
    LogException(nested);
  } catch (...) {
    zit::logger()->error("Unknown nested torrent exception");
  }
}

}  // namespace

TorrentListModel::TorrentListModel()
    : file_writer_thread_(
          std::make_unique<zit::FileWriterThread>([](zit::Torrent& torrent) {
            zit::logger()->info("Download completed of {}. Continuing to seed.",
                                torrent.name());
          })) {
  RefreshMenuEntries();
}

TorrentListModel::~TorrentListModel() {
  StopAllTorrents();
}

bool TorrentListModel::empty() const {
  return torrents_.empty();
}

const std::vector<TorrentInfo>& TorrentListModel::torrents() const {
  return torrents_;
}

std::vector<TorrentInfo>& TorrentListModel::torrents() {
  return torrents_;
}

const std::vector<std::string>& TorrentListModel::menu_entries() const {
  return menu_entries_;
}

std::vector<std::string>& TorrentListModel::menu_entries() {
  return menu_entries_;
}

int TorrentListModel::selected_index() const {
  return selected_index_;
}

int* TorrentListModel::mutable_selected_index() {
  return &selected_index_;
}

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

bool TorrentListModel::LaunchTorrent(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    zit::logger()->error("Torrent file '{}' does not exist", path.string());
    return false;
  }

  try {
    auto torrent = std::make_unique<zit::Torrent>(m_io_context, path,
                                                  std::filesystem::path{});
    file_writer_thread_->register_torrent(*torrent);

    auto active = std::make_unique<ActiveTorrent>();
    active->source_path = path;
    active->last_completed_pieces = 0;
    auto* torrent_ptr = torrent.get();
    active->torrent = std::move(torrent);
    active->worker = std::thread([torrent_ptr]() {
      try {
        torrent_ptr->start();
        torrent_ptr->run();
      } catch (const std::exception& ex) {
        LogException(ex);
      }
    });

    {
      const std::scoped_lock lock(active_mutex_);
      active_torrents_.push_back(std::move(active));
    }

    return true;
  } catch (const std::exception& ex) {
    zit::logger()->error("Failed to add torrent '{}': {}", path.string(),
                         ex.what());
  }
  return false;
}

void TorrentListModel::StopAllTorrents() {
  std::vector<std::unique_ptr<ActiveTorrent>> to_cleanup;
  {
    const std::scoped_lock lock(active_mutex_);
    to_cleanup = std::move(active_torrents_);
  }

  for (auto& active : to_cleanup) {
    if (active && active->torrent) {
      try {
        active->torrent->stop();
      } catch (const std::exception& ex) {
        zit::logger()->warn("Error stopping torrent '{}': {}",
                            active->source_path.string(), ex.what());
      }
    }
  }

  for (auto& active : to_cleanup) {
    if (active && active->worker.joinable()) {
      active->worker.join();
    }
  }
}

std::vector<TorrentInfo> TorrentListModel::CollectSnapshot() {
  std::vector<TorrentInfo> snapshot;
  const auto now = Clock::now();

  const std::scoped_lock lock(active_mutex_);
  snapshot.reserve(active_torrents_.size());

  for (auto& active : active_torrents_) {
    if (!active->torrent) {
      continue;
    }

    const auto [completed_pieces, total_pieces] =
        active->torrent->piece_status();
    double percent = 0.0;
    if (total_pieces > 0) {
      percent = (static_cast<double>(completed_pieces) /
                 static_cast<double>(total_pieces)) *
                100.0;
    }

    const auto length = active->torrent->length();
    const auto peers = active->torrent->peer_count();

    double bytes_per_second = 0.0;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - active->last_snapshot)
                             .count();
    if (elapsed > 0) {
      const auto pieces_delta =
          static_cast<int64_t>(completed_pieces) -
          static_cast<int64_t>(active->last_completed_pieces);
      if (pieces_delta > 0) {
        const auto bytes_delta =
            pieces_delta *
            static_cast<int64_t>(active->torrent->piece_length());
        bytes_per_second = static_cast<double>(bytes_delta) /
                           (static_cast<double>(elapsed) / 1000.0);
      }
    }
    active->last_completed_pieces = completed_pieces;
    active->last_snapshot = now;

    snapshot.push_back(TorrentInfo{
        .name = active->torrent->name(),
        .complete = fmt::format("{:.1f}%", percent),
        .size = FormatBytes(length),
        .peers = fmt::format("{}", peers),
        .down_speed = FormatRate(bytes_per_second),
        .up_speed = "-",
    });
  }

  return snapshot;
}

void TorrentListModel::OnPolledSnapshot(std::vector<TorrentInfo> snapshot) {
  torrents_ = std::move(snapshot);
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
