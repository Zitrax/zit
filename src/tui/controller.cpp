#include "controller.hpp"

#include <algorithm>
#include <chrono>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include "../global_config.hpp"
#include "tui_logger.hpp"
#include "view/components.hpp"

namespace zit::tui {

using namespace ftxui;

namespace {
constexpr bool kEnableTestLogThread = false;
}

TuiController::TuiController(bool clean_start)
    : screen_(ScreenInteractive::Fullscreen()) {
  BuildComponents();
  BindEvents();

  // Load previously saved torrents unless --clean was passed
  if (!clean_start) {
    const auto& config = zit::SingletonDirectoryFileConfig::getInstance();
    const auto saved_torrents =
        config.get(zit::StringListSetting::TUI_TORRENTS);

    // Parse each entry as "torrent_path:data_dir"
    for (const auto& entry : saved_torrents) {
      const auto colon_pos = entry.find(':');
      if (colon_pos != std::string::npos) {
        const auto path = entry.substr(0, colon_pos);
        const auto data_dir = entry.substr(colon_pos + 1);
        zit::tui::logger()->info("Resuming torrent: {} (data: {})", path,
                                 data_dir);
        LaunchTorrent(path, data_dir);
      } else {
        // Fallback for old format or malformed entries
        zit::tui::logger()->info("Resuming torrent: {} (data: cwd)", entry);
        LaunchTorrent(entry, std::filesystem::current_path());
      }
    }
  }

  StartSnapshotThread();
  if constexpr (kEnableTestLogThread) {
    StartTestLogThread();
  }
}

TuiController::~TuiController() {
  Shutdown();
}

int TuiController::Run() {
  screen_.Loop(main_renderer_);
  Shutdown();
  return 0;
}

void TuiController::BuildComponents() {
  MenuOption menu_option;
  menu_option.on_enter = [this] {
    if (!model_.empty()) {
      show_details_ = !show_details_;
    }
  };

  menu_component_ = Menu(&model_.menu_entries(),
                         model_.mutable_selected_index(), menu_option);

  menu_renderer_ = Renderer(menu_component_, [this] {
    return view::RenderTorrentTable(model_, model_.selected_index());
  });

  detail_renderer_ = Renderer([this] {
    return view::RenderDetailPanel(model_, model_.selected_index());
  });

  log_renderer_ = view::MakeLogPanel([this] { return screen_.dimy(); });

  main_container_ = Container::Vertical({
      menu_renderer_,
      Maybe(log_renderer_, &show_log_),
  });

  main_renderer_ = Renderer(main_container_, [this] {
    auto content = menu_renderer_->Render() | flex;

    if (show_details_ && !model_.empty() && !show_log_) {
      content = vbox({
          menu_renderer_->Render() | flex,
          detail_renderer_->Render(),
      });
    }

    if (show_log_) {
      const int window_height = std::max(1, screen_.dimy());
      const int half_height = std::max(1, window_height / 2);
      content = vbox({
          content | size(HEIGHT, EQUAL, half_height),
          log_renderer_->Render() | size(HEIGHT, EQUAL, half_height),
      });
    }

    return content;
  });

  file_dialog_ = view::MakeFileDialog(
      [this](const std::filesystem::path& path) {
        LaunchTorrent(path, std::filesystem::current_path());
      },
      [this] { open_dialog_ = false; });

  help_modal_ = view::MakeHelpModal();

  main_renderer_ |= Modal(file_dialog_, &open_dialog_);
  main_renderer_ |= Modal(help_modal_, &show_help_);
}

void TuiController::BindEvents() {
  main_renderer_ |= CatchEvent([this](Event event) {
    if (event == Event::Special("zit.snapshot")) {
      HandleSnapshotEvent();
      return true;
    }
    if (event == Event::Character('q') || event == Event::Escape) {
      screen_.Exit();
      return true;
    }
    if (event == Event::Character('o')) {
      open_dialog_ = true;
      return true;
    }
    if (event == Event::Character('l')) {
      show_log_ = !show_log_;
      return true;
    }
    if (event == Event::Character('h')) {
      show_help_ = !show_help_;
      return true;
    }
    if (event == Event::Character('d')) {
      if (!model_.empty()) {
        model_.StopTorrent(model_.selected_index());
        model_.OnPolledSnapshot(model_.CollectSnapshot());
      }
      return true;
    }
    if (open_dialog_ || show_help_) {
      // Let dialogs process escape to close
      if (event == Event::Escape) {
        if (show_help_) {
          show_help_ = false;
        } else if (open_dialog_) {
          open_dialog_ = false;
        }
        return true;
      }
      // Let the dialogs process other navigation/selection keys.
      return false;
    }
    if (event == Event::Return) {
      if (!model_.empty()) {
        show_details_ = !show_details_;
      }
      return true;
    }
    return false;
  });
}

void TuiController::LaunchTorrent(const std::filesystem::path& path,
                                  const std::filesystem::path& data_dir) {
  if (model_.LaunchTorrent(path, data_dir)) {
    model_.OnPolledSnapshot(model_.CollectSnapshot());
  }
}

void TuiController::StartSnapshotThread() {
  snapshot_thread_ = std::thread([this] { SnapshotLoop(); });
}

void TuiController::StartTestLogThread() {
  if constexpr (!kEnableTestLogThread) {
    return;
  }
  test_log_thread_ = std::thread([this] { TestLogLoop(); });
}

void TuiController::SnapshotLoop() {
  using namespace std::chrono_literals;
  while (keep_polling_) {
    auto snapshot = model_.CollectSnapshot();
    {
      const std::scoped_lock lock(snapshot_mutex_);
      pending_snapshot_ = std::move(snapshot);
      snapshot_ready_ = true;
    }
    screen_.PostEvent(Event::Special("zit.snapshot"));

    for (int i = 0; i < 50 && keep_polling_; ++i) {
      std::this_thread::sleep_for(20ms);
    }
  }
}

void TuiController::TestLogLoop() {
  if constexpr (!kEnableTestLogThread) {
    return;
  }
  using namespace std::chrono_literals;
  std::size_t counter = 0;
  while (keep_polling_) {
    zit::tui::zit_logger()->info("[test] Generated log message {}", counter++);
    std::this_thread::sleep_for(500ms);
  }
}

void TuiController::HandleSnapshotEvent() {
  std::vector<TorrentInfo> snapshot;
  {
    const std::scoped_lock lock(snapshot_mutex_);
    if (!snapshot_ready_) {
      return;
    }
    snapshot = std::move(pending_snapshot_);
    snapshot_ready_ = false;
  }
  model_.OnPolledSnapshot(std::move(snapshot));
}

void TuiController::Shutdown() {
  if (shutdown_requested_.exchange(true)) {
    return;
  }
  keep_polling_ = false;
  if (snapshot_thread_.joinable()) {
    snapshot_thread_.join();
  }
  if constexpr (kEnableTestLogThread) {
    if (test_log_thread_.joinable()) {
      test_log_thread_.join();
    }
  }

  // Save current torrents as "torrent_path:data_dir" format
  const auto torrent_paths = model_.GetTorrentPaths();
  const auto data_dirs = model_.GetDataDirectories();

  std::vector<std::string> combined_entries;
  for (size_t i = 0; i < torrent_paths.size(); ++i) {
    const auto& path = torrent_paths[i];
    const auto& data_dir =
        (i < data_dirs.size()) ? data_dirs[i] : std::filesystem::current_path();
    combined_entries.push_back(path.string() + ":" + data_dir.string());
  }

  auto& config = zit::SingletonDirectoryFileConfig::getInstance();
  config.set(zit::StringListSetting::TUI_TORRENTS, combined_entries);
  config.save();

  model_.StopAllTorrents();
}

}  // namespace zit::tui
