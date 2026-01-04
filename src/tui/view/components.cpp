#include "components.hpp"
#include "../model.hpp"
#include "../tui_logger.hpp"

#include <fmt/format.h>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace zit::tui::view {

using namespace ftxui;

ButtonOption TextOnlyButtonOption(Color color_active) {
  auto option = ButtonOption::Simple();
  option.transform = [color_active](const EntryState& s) {
    auto element = text(s.label);
    if (s.focused) {
      element |= color(color_active) | bgcolor(Color::Grey23) | bold;
    } else {
      element |= color(color_active);
    }
    return element;
  };
  return option;
}

Component MakeFileDialog(
    const std::function<void(const std::filesystem::path&)>& open,
    const std::function<void()>& close) {
  auto container = Container::Vertical({});

  auto renderer = Renderer(container, [container, open, close,
                                       populate = true]() mutable {
    if (populate) {
      std::cerr << "Populating file dialog\n";
      container->DetachAllChildren();

      if (std::filesystem::current_path() !=
          std::filesystem::current_path().root_path()) {
        container->Add(Button(
            "..",
            [&populate]() {
              std::filesystem::current_path(
                  std::filesystem::current_path().parent_path());
              populate = true;
            },
            TextOnlyButtonOption(Color::Magenta)));
      }

      for (auto const& dir_entry : std::filesystem::directory_iterator{"."}) {
        if (dir_entry.is_regular_file() &&
            dir_entry.path().extension() != ".torrent") {
          continue;
        }
        container->Add(Button(
            dir_entry.path().filename().string(),
            [open, close, path = dir_entry.path(), &populate]() {
              if (std::filesystem::is_directory(path)) {
                std::filesystem::current_path(path);
                populate = true;
                return;
              }
              open(path);
              close();
            },
            TextOnlyButtonOption(std::filesystem::is_directory(dir_entry.path())
                                     ? Color::Magenta
                                     : Color::Green)));
      }
      container->Add(Button(
          "Cancel", [close] { close(); }, TextOnlyButtonOption(Color::Red)));

      if (container->ChildCount() > 0) {
        container->SetActiveChild(container->ChildAt(0));
      }

      populate = false;
    }
    return vbox({
               text("Open torrent"),
               separator(),
               container->Render(),
           })                               //
           | size(WIDTH, GREATER_THAN, 30)  //
           | border;                        //
  });
  return renderer;
}

Element RenderTorrentTable(const TorrentListModel& model, int selected_index) {
  Elements rows;

  rows.push_back(hbox({
      text("Name") | bold | flex,
      separator(),
      text("Complete") | bold | size(WIDTH, EQUAL, 10),
      separator(),
      text("Size") | bold | size(WIDTH, EQUAL, 12),
      separator(),
      text("Peers") | bold | size(WIDTH, EQUAL, 8),
      separator(),
      text("Down Speed") | bold | size(WIDTH, EQUAL, 12),
      separator(),
      text("Up Speed") | bold | size(WIDTH, EQUAL, 12),
  }));

  rows.push_back(separator());

  if (model.empty()) {
    rows.push_back(text("No torrents. Press 'o' to add one.") | center | focus);
  } else {
    for (size_t i = 0; i < model.torrents().size(); ++i) {
      const auto& torrent = model.torrents().at(i);
      const bool is_selected = std::cmp_equal(i, selected_index);

      auto row = hbox({
          text(torrent.name) | flex,
          separator(),
          text(torrent.complete) | size(WIDTH, EQUAL, 10),
          separator(),
          text(torrent.size) | size(WIDTH, EQUAL, 12),
          separator(),
          text(torrent.peers) | size(WIDTH, EQUAL, 8),
          separator(),
          text(torrent.down_speed) | size(WIDTH, EQUAL, 12),
          separator(),
          text(torrent.up_speed) | size(WIDTH, EQUAL, 12),
      });

      if (is_selected) {
        row = row | bgcolor(Color::DarkRed) | focus;
      }

      rows.push_back(row);
    }
  }

  return vbox(std::move(rows)) | border | vscroll_indicator | frame;
}

Element RenderDetailPanel(const TorrentListModel& model, int selected_index) {
  if (model.empty()) {
    return vbox({
               separator(),
               text("No torrent selected") | center,
           }) |
           border | flex;
  }

  const auto* torrent = model.selected();
  if (!torrent) {
    return vbox({text("Selection out of range") | center}) | border | flex;
  }

  (void)selected_index;  // placeholder until extended detail view arrives.

  return vbox({
             separator(),
             text("Details for: " + torrent->name) | bold | center,
             separator(),
             hbox(text("Data Directory: "),
                  text(torrent->data_directory) | dim),
             text("(More information will be added here later)") | dim | center,
         }) |
         border | flex;
}

namespace {

class LogPanelBase : public ComponentBase {
 public:
  explicit LogPanelBase(std::function<int()> window_height_provider)
      : window_height_provider_(std::move(window_height_provider)) {}

  bool OnEvent(Event event) override {
    if (event == Event::ArrowUp) {
      scroll_y_ = std::max(0, scroll_y_ - 1);
      return true;
    }
    if (event == Event::ArrowDown) {
      scroll_y_++;
      return true;
    }
    if (event == Event::ArrowLeft) {
      scroll_x_ = std::max(0, scroll_x_ - 1);
      return true;
    }
    if (event == Event::ArrowRight) {
      scroll_x_++;
      return true;
    }
    if (event == Event::PageUp) {
      scroll_y_ = std::max(0, scroll_y_ - 10);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_y_ += 10;
      return true;
    }
    if (event == Event::Home) {
      scroll_x_ = 0;
      scroll_y_ = 0;
      return true;
    }
    return ComponentBase::OnEvent(event);
  }

  Element OnRender() override {
    UpdateLogs();

    const int window_height =
        window_height_provider_ ? std::max(1, window_height_provider_()) : 1;
    const int quarter_height = std::max(1, window_height / 4);

    return vbox({
               text("Main Logger:") | bold,
               separator(),
               RenderLogView(main_log_items_, main_log_levels_, quarter_height),
               separator(),
               text("File Writer Logger:") | bold,
               separator(),
               RenderLogView(file_log_items_, file_log_levels_, quarter_height),
           }) |
           border | flex;
  }

 private:
  [[nodiscard]] Element RenderLogView(
      const std::vector<std::string>& items,
      const std::vector<spdlog::level::level_enum>& levels,
      int quarter_height) const {
    // Clamp scroll_y_ to valid range
    const int max_scroll_y = std::max(0, static_cast<int>(items.size()) - 1);
    const int effective_scroll_y = std::clamp(scroll_y_, 0, max_scroll_y);

    // Calculate visible range
    const auto start_line = static_cast<size_t>(effective_scroll_y);
    const size_t end_line = std::min(
        items.size(), start_line + static_cast<size_t>(quarter_height));

    Elements lines;
    for (size_t i = start_line; i < end_line; ++i) {
      Color c = Color::White;
      if (i < levels.size()) {
        switch (levels.at(i)) {
          case spdlog::level::trace:
            c = Color::Grey50;
            break;
          case spdlog::level::debug:
            c = Color::Cyan;
            break;
          case spdlog::level::info:
            c = Color::Green;
            break;
          case spdlog::level::warn:
            c = Color::Yellow;
            break;
          case spdlog::level::err:
            c = Color::Red;
            break;
          case spdlog::level::critical:
            c = Color::RedLight;
            break;
          case spdlog::level::off:
          case spdlog::level::n_levels:
            c = Color::White;
            break;
        }
      }

      // Apply horizontal scroll by substring
      std::string line_text = items.at(i);
      if (scroll_x_ > 0 && static_cast<size_t>(scroll_x_) < line_text.size()) {
        line_text = line_text.substr(static_cast<size_t>(scroll_x_));
      } else if (std::cmp_greater_equal(scroll_x_, line_text.size())) {
        line_text = "";
      }

      lines.push_back(text(line_text) | color(c));
    }

    if (lines.empty()) {
      lines.push_back(text("") | dim);
    }

    return vbox(std::move(lines)) | frame | size(HEIGHT, EQUAL, quarter_height);
  }

  void UpdateLogs() {
    auto update_one = [](const std::shared_ptr<spdlog::logger>& logger,
                         std::vector<std::string>& items,
                         std::vector<spdlog::level::level_enum>& levels) {
      items.clear();
      levels.clear();
      auto sinks = logger->sinks();
      for (const auto& sink : sinks) {
        auto ringbuffer_sink =
            std::dynamic_pointer_cast<spdlog::sinks::ringbuffer_sink_mt>(sink);
        if (ringbuffer_sink) {
          const auto entries = ringbuffer_sink->last_raw();
          for (const auto& msg : entries) {
            auto level_str =
                std::string("[") + to_string_view(msg.level).data() + "]";
            auto payload_str =
                std::string(msg.payload.data(), msg.payload.size());
            items.push_back(fmt::format("{} {}", level_str, payload_str));
            levels.push_back(msg.level);
          }
        }
      }
      return static_cast<int>(items.size());
    };

    update_one(zit::tui::zit_logger(), main_log_items_, main_log_levels_);
    update_one(zit::tui::file_writer_logger(), file_log_items_,
               file_log_levels_);
  }

  std::function<int()> window_height_provider_;
  std::vector<std::string> main_log_items_;
  std::vector<spdlog::level::level_enum> main_log_levels_;

  std::vector<std::string> file_log_items_;
  std::vector<spdlog::level::level_enum> file_log_levels_;

  int scroll_x_ = 0;
  int scroll_y_ = 0;
};

}  // namespace

Component MakeLogPanel(std::function<int()> window_height_provider) {
  return std::make_shared<LogPanelBase>(std::move(window_height_provider));
}

Component MakeHelpModal() {
  auto button = Button("Close", []() {}, ButtonOption::Simple());
  auto container = Container::Vertical({button});

  return Renderer(container, [=] {
    return vbox({
               text("ZIT TUI - Keyboard Shortcuts") | bold | center,
               separator(),
               text(""),
               hbox(text("  o"), text(" - Open torrent file"), filler()),
               hbox(text("  d"), text(" - Delete selected torrent"), filler()),
               hbox(text("  l"), text(" - Toggle log panel"), filler()),
               hbox(text("  h"), text(" - Show this help"), filler()),
               hbox(text("  ENTER"), text(" - Toggle details panel"), filler()),
               hbox(text("  TAB/SHIFT+TAB"), text(" - Navigate menu"),
                    filler()),
               hbox(text("  q/ESC"), text(" - Quit"), filler()),
               text(""),
               separator(),
               text(""),
               text("Torrents are automatically saved and resumed on startup."),
               text("Use --clean flag to skip resuming saved torrents."),
               text(""),
           }) |
           size(WIDTH, GREATER_THAN, 50) | border;
  });
}

}  // namespace zit::tui::view
