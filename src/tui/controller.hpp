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
  explicit TuiController(bool clean_start = false);
  ~TuiController();

  int Run();

 private:
  void BuildComponents();
  void BindEvents();
  void LaunchTorrent(const std::filesystem::path& path, 
                     const std::filesystem::path& data_dir = std::filesystem::current_path());
  void StartSnapshotThread();
  void StartTestLogThread();
  void SnapshotLoop();
  void TestLogLoop();
  void HandleSnapshotEvent();
  void Shutdown();

  TorrentListModel model_;
  bool show_log_ = false;
  bool show_details_ = false;
  bool open_dialog_ = false;
  bool show_help_ = false;

  ftxui::ScreenInteractive screen_;
  ftxui::Component log_renderer_;
  ftxui::Component menu_component_;
  ftxui::Component menu_renderer_;
  ftxui::Component detail_renderer_;
  ftxui::Component main_container_;
  ftxui::Component main_renderer_;
  ftxui::Component file_dialog_;
  ftxui::Component help_modal_;

  std::atomic<bool> keep_polling_{true};
  std::atomic<bool> shutdown_requested_{false};
  std::thread snapshot_thread_;
  std::thread test_log_thread_;
  std::mutex snapshot_mutex_;
  std::vector<TorrentInfo> pending_snapshot_;
  bool snapshot_ready_ = false;
};

}  // namespace zit::tui
