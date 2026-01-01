#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "model.hpp"

namespace zit::tui {

class TuiController {
 public:
  TuiController();
  ~TuiController();

  int Run();

 private:
  void BuildComponents();
  void BindEvents();
  void LaunchTorrent(const std::filesystem::path& path);
  void StartSnapshotThread();
  void SnapshotLoop();
  void HandleSnapshotEvent();
  void Shutdown();

  TorrentListModel model_;
  bool show_details_ = false;
  bool open_dialog_ = false;

  ftxui::ScreenInteractive screen_;
  ftxui::Component menu_component_;
  ftxui::Component menu_renderer_;
  ftxui::Component detail_renderer_;
  ftxui::Component main_container_;
  ftxui::Component main_renderer_;
  ftxui::Component file_dialog_;

  std::atomic<bool> keep_polling_{true};
  std::atomic<bool> shutdown_requested_{false};
  std::thread snapshot_thread_;
  std::mutex snapshot_mutex_;
  std::vector<TorrentInfo> pending_snapshot_;
  bool snapshot_ready_ = false;
};

}  // namespace zit::tui
