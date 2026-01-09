#include "model.hpp"

#include "file_writer.hpp"
#include "tui_logger.hpp"

#include <sha1.hpp>
#include <torrent.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zit::tui {

namespace {

using Clock = std::chrono::steady_clock;

// Generate a compressed piece visualization using Unicode blocks
// Groups multiple pieces per character and represents state density
// Format creation date as human-readable string
std::string FormatCreationDate(int64_t timestamp) {
  if (timestamp <= 0) {
    return "Unknown";
  }
  // Simple timestamp to ISO-like format - no fancy timezone handling
  return fmt::format("{}", timestamp);
}

// Format info hash as hex string
std::string FormatInfoHash(const zit::Sha1& hash) {
  return hash.hex();
}

std::string FormatBytes(int64_t bytes) {
  static constexpr std::array<const char*, 5> kSuffixes{"B", "KiB", "MiB",
                                                        "GiB", "TiB"};
  auto value = static_cast<double>(bytes);
  size_t idx = 0;
  while (value >= 1024.0 && idx + 1 < kSuffixes.size()) {
    value /= 1024.0;
    ++idx;
  }
  return fmt::format("{:.1f} {}", value, kSuffixes.at(idx));
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
  return fmt::format("{:.1f} {}", value, kSuffixes.at(idx));
}

std::string PlaceholderLabel(const std::filesystem::path& path) {
  auto name = path.filename().string();
  if (name.empty()) {
    name = path.string();
  }
  return fmt::format("{} (loading...)", name);
}

TorrentInfo MakePlaceholderInfo(const std::filesystem::path& path) {
  return TorrentInfo{
      .name = PlaceholderLabel(path),
      .complete = "-",
      .size = "-",
      .peers = "-",
      .down_speed = "-",
      .up_speed = "-",
      .data_directory = "-",
      .announce = "-",
      .creation_date = "-",
      .comment = "-",
      .created_by = "-",
      .encoding = "-",
      .piece_count = "-",
      .piece_length = "-",
      .is_private = "-",
      .info_hash = "-",
      .files_info = "-",
      .pieces_completed = 0,
      .pieces_total = 0,
  };
}

void LogException(const std::exception& ex) {
  zit_logger()->error("Torrent thread error: {}", ex.what());
  try {
    std::rethrow_if_nested(ex);
  } catch (const std::exception& nested) {
    LogException(nested);
  } catch (...) {
    zit_logger()->error("Unknown nested torrent exception");
  }
}

}  // namespace

TorrentListModel::TorrentListModel()
    : file_writer_thread_(
          std::make_unique<zit::FileWriterThread>([](zit::Torrent& torrent) {
            zit_logger()->info("Download completed of {}. Continuing to seed.",
                               torrent.name());
          })) {
  RefreshMenuEntries();
  StartCreationThread();
}

TorrentListModel::~TorrentListModel() {
  StopCreationThread();
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
  if (std::cmp_less(selected_index_, 0) ||
      std::cmp_greater_equal(selected_index_, torrents_.size())) {
    return nullptr;
  }
  return &torrents_.at(static_cast<size_t>(selected_index_));
}

bool TorrentListModel::LaunchTorrent(const std::filesystem::path& path,
                                     const std::filesystem::path& data_dir) {
  if (!std::filesystem::exists(path)) {
    zit_logger()->error("Torrent file '{}' does not exist", path.string());
    return false;
  }
  const auto placeholder_info = MakePlaceholderInfo(path);
  {
    const std::scoped_lock lock(pending_mutex_);
    pending_requests_.emplace(path, data_dir);
    pending_placeholders_.emplace_back(path, data_dir, placeholder_info);
  }
  pending_cv_.notify_one();

  torrents_.push_back(placeholder_info);
  RefreshMenuEntries();
  return true;
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
        zit_logger()->warn("Error stopping torrent '{}': {}",
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

void TorrentListModel::StopTorrent(int index) {
  if (index < 0 || std::cmp_greater_equal(index, torrents_.size())) {
    return;
  }

  std::unique_ptr<ActiveTorrent> to_cleanup;
  {
    const std::scoped_lock lock(active_mutex_);
    if (std::cmp_greater_equal(index, active_torrents_.size())) {
      return;
    }
    to_cleanup = std::move(active_torrents_.at(static_cast<size_t>(index)));
    active_torrents_.erase(active_torrents_.begin() + index);
  }

  if (to_cleanup && to_cleanup->torrent) {
    try {
      to_cleanup->torrent->stop();
    } catch (const std::exception& ex) {
      zit_logger()->warn("Error stopping torrent '{}': {}",
                         to_cleanup->source_path.string(), ex.what());
    }
  }

  if (to_cleanup && to_cleanup->worker.joinable()) {
    to_cleanup->worker.join();
  }

  torrents_.erase(torrents_.begin() + index);
  ClampSelection();
  RefreshMenuEntries();
}

std::vector<std::filesystem::path> TorrentListModel::GetTorrentPaths() const {
  std::vector<std::filesystem::path> paths;
  const std::scoped_lock lock(active_mutex_);
  paths.reserve(active_torrents_.size());
  for (const auto& active : active_torrents_) {
    if (active) {
      paths.push_back(active->source_path);
    }
  }
  return paths;
}

std::vector<std::filesystem::path> TorrentListModel::GetDataDirectories()
    const {
  std::vector<std::filesystem::path> dirs;
  const std::scoped_lock lock(active_mutex_);
  dirs.reserve(active_torrents_.size());
  for (const auto& active : active_torrents_) {
    if (active) {
      dirs.push_back(active->data_directory);
    }
  }
  return dirs;
}

void TorrentListModel::StartCreationThread() {
  creation_thread_ = std::thread([this] { CreationLoop(); });
}

void TorrentListModel::StopCreationThread() {
  keep_creating_ = false;
  pending_cv_.notify_all();
  if (creation_thread_.joinable()) {
    creation_thread_.join();
  }
}

void TorrentListModel::CreationLoop() {
  while (true) {
    PendingRequest request;
    {
      std::unique_lock lock(pending_mutex_);
      pending_cv_.wait(lock, [this] {
        return !keep_creating_ || !pending_requests_.empty();
      });
      if (!keep_creating_ && pending_requests_.empty()) {
        return;
      }
      request = std::move(pending_requests_.front());
      pending_requests_.pop();
    }

    CreateAndStartTorrent(request.torrent_path, request.data_directory);
    RemovePlaceholderForPath(request.torrent_path);
  }
}

bool TorrentListModel::CreateAndStartTorrent(
    const std::filesystem::path& path,
    const std::filesystem::path& data_dir) {
  try {
    auto torrent = std::make_unique<zit::Torrent>(m_io_context, path, data_dir);
    file_writer_thread_->register_torrent(*torrent);

    auto active = std::make_unique<ActiveTorrent>();
    // Store absolute path so it can be resumed later
    active->source_path = std::filesystem::absolute(path);
    active->data_directory = data_dir;
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
    zit_logger()->error("Failed to add torrent '{}': {}", path.string(),
                        ex.what());
  }
  return false;
}

void TorrentListModel::RemovePlaceholderForPath(
    const std::filesystem::path& path) {
  const std::scoped_lock lock(pending_mutex_);
  auto it = std::ranges::remove_if(pending_placeholders_,
                                   [&path](const PendingPlaceholder& pending) {
                                     return pending.source_path == path;
                                   });
  pending_placeholders_.erase(it.begin(), it.end());
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

    // Extract metadata
    const auto& announce = active->torrent->announce();
    const auto& comment = active->torrent->comment();
    const auto& created_by = active->torrent->created_by();
    const auto& encoding = active->torrent->encoding();
    const auto creation_date = active->torrent->creation_date();
    const auto piece_length = active->torrent->piece_length();
    const bool is_private = active->torrent->is_private();
    const auto& info_hash = active->torrent->info_hash();
    const auto& files = active->torrent->files();

    // Format files info for multi-file torrents
    std::string files_info;
    if (!files.empty()) {
      files_info = fmt::format("{} files", files.size());
    } else if (active->torrent->is_single_file()) {
      files_info = "Single file";
    } else {
      files_info = "Unknown";
    }

    snapshot.push_back(TorrentInfo{
        .name = active->torrent->name(),
        .complete = fmt::format("{:.1f}%", percent),
        .size = FormatBytes(length),
        .peers = fmt::format("{}", peers),
        .down_speed = FormatRate(bytes_per_second),
        .up_speed = "-",
        .data_directory = active->data_directory.string(),
        .announce = announce.empty() ? "None" : announce,
        .creation_date = FormatCreationDate(creation_date),
        .comment = comment.empty() ? "None" : comment,
        .created_by = created_by.empty() ? "None" : created_by,
        .encoding = encoding.empty() ? "UTF-8" : encoding,
        .piece_count = fmt::format("{}", total_pieces),
        .piece_length = FormatBytes(piece_length),
        .is_private = is_private ? "Yes" : "No",
        .info_hash = FormatInfoHash(info_hash),
        .files_info = files_info,
        .pieces_completed = static_cast<uint32_t>(completed_pieces),
        .pieces_total = static_cast<uint32_t>(total_pieces),
    });
  }

  {
    const std::scoped_lock pending_lock(pending_mutex_);
    for (const auto& placeholder : pending_placeholders_) {
      snapshot.push_back(placeholder.info);
    }
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
  selected_index_ = std::max(selected_index_, 0);
  const int max_index = static_cast<int>(menu_entries_.size()) - 1;
  selected_index_ = std::min(selected_index_, max_index);
}

}  // namespace zit::tui
