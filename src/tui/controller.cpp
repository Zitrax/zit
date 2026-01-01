#include "controller.hpp"

#include <chrono>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include "view/components.hpp"

namespace zit::tui {

using namespace ftxui;

TuiController::TuiController() : screen_(ScreenInteractive::Fullscreen()) {
  BuildComponents();
  BindEvents();
  StartSnapshotThread();
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

  log_renderer_ = Renderer([] { return view::RenderLogPanel(); });

  main_container_ = Container::Vertical({menu_renderer_});

  main_renderer_ = Renderer(main_container_, [this] {
    auto content = menu_renderer_->Render() | flex;

    if (show_details_ && !model_.empty() && !show_log_) {
      content = vbox({
          menu_renderer_->Render() | flex,
          detail_renderer_->Render(),
      });
    }

    if (show_log_) {
      content = vbox({
          content,
          log_renderer_->Render(),
      });
    }

    return content;
  });

  file_dialog_ = view::MakeFileDialog(
      [this](const std::filesystem::path& path) { LaunchTorrent(path); },
      [this] { open_dialog_ = false; });

  main_renderer_ |= Modal(file_dialog_, &open_dialog_);
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
    if (open_dialog_) {
      // Let the file dialog process navigation/selection keys.
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

void TuiController::LaunchTorrent(const std::filesystem::path& path) {
  if (model_.LaunchTorrent(path)) {
    model_.OnPolledSnapshot(model_.CollectSnapshot());
  }
}

void TuiController::StartSnapshotThread() {
  snapshot_thread_ = std::thread([this] { SnapshotLoop(); });
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
  model_.StopAllTorrents();
}

}  // namespace zit::tui
