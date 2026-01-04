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
#include <limits>
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
    if (auto_mode_) {
      // Transition from auto-follow to manual scrolling; start from last auto
      // positions.
      auto_mode_ = false;
      scroll_main_ = auto_scroll_main_;
      scroll_file_ = auto_scroll_file_;
    }

    // Helper to clamp scroll values to valid range [0, size - height]
    auto clamp_scroll = [](int& scroll_y, const std::vector<std::string>& items,
                           int height) {
      const int max_scroll =
          std::max(0, static_cast<int>(items.size()) - height);
      scroll_y = std::clamp(scroll_y, 0, max_scroll);
    };

    const int log_height = std::max(1, (window_height_provider_() / 4) - 6);

    if (event == Event::ArrowUp) {
      scroll_main_ = std::max(0, scroll_main_ - 1);
      scroll_file_ = std::max(0, scroll_file_ - 1);
      return true;
    }
    if (event == Event::ArrowDown) {
      scroll_main_++;
      scroll_file_++;
      // Clamp to prevent scrolling past the end
      clamp_scroll(scroll_main_, main_log_items_, log_height);
      clamp_scroll(scroll_file_, file_log_items_, log_height);
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
      scroll_main_ = std::max(0, scroll_main_ - 10);
      scroll_file_ = std::max(0, scroll_file_ - 10);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_main_ += 10;
      scroll_file_ += 10;
      // Clamp to prevent scrolling past the end
      clamp_scroll(scroll_main_, main_log_items_, log_height);
      clamp_scroll(scroll_file_, file_log_items_, log_height);
      return true;
    }
    if (event == Event::End) {
      // Jump to bottom for both logs
      scroll_main_ = std::numeric_limits<int>::max();
      scroll_file_ = std::numeric_limits<int>::max();
      // Clamp immediately to valid range
      clamp_scroll(scroll_main_, main_log_items_, log_height);
      clamp_scroll(scroll_file_, file_log_items_, log_height);
      return true;
    }
    if (event == Event::Home) {
      scroll_x_ = 0;
      scroll_main_ = 0;
      scroll_file_ = 0;
      return true;
    }
    return ComponentBase::OnEvent(event);
  }

  // Called when the log view is not focused to auto-scroll to bottom
  void AutoFollowBottom([[maybe_unused]] int viewport_height) {
    // Set scroll_y_ to -1 to signal auto-follow mode
    // Each log section will independently show its bottom lines
    auto_mode_ = true;
  }

  Element OnRender() override {
    UpdateLogs();

    const int window_height =
        window_height_provider_ ? std::max(1, window_height_provider_()) : 1;
    // Offset to account for frame borders and other overhead that reduces
    // visible height. This is empirically determined to show all log lines
    // without truncation.
    constexpr int height_overhead = 6;
    const int quarter_height =
        std::max(1, (window_height / 4) - height_overhead);

    return vbox({
               text("Main Logger:") | bold,
               separator(),
               RenderLogView(main_log_items_, main_log_levels_, quarter_height,
                             true),
               separator(),
               text("File Writer Logger:") | bold,
               separator(),
               RenderLogView(file_log_items_, file_log_levels_, quarter_height,
                             false),
           }) |
           border | flex;
  }

 private:
  [[nodiscard]] Element RenderLogView(
      const std::vector<std::string>& items,
      const std::vector<spdlog::level::level_enum>& levels,
      int quarter_height,
      bool is_main_logger) {
    // Calculate scroll position for this specific log section
    int effective_scroll_y = 0;
    if (auto_mode_) {
      // Auto-follow mode: show the bottom lines of THIS log section
      const int total_lines = static_cast<int>(items.size());
      if (total_lines > quarter_height) {
        effective_scroll_y = total_lines - quarter_height;
      } else {
        effective_scroll_y = 0;
      }
      // Remember per-section auto position for later manual resume
      if (is_main_logger) {
        auto_scroll_main_ = effective_scroll_y;
      } else {
        auto_scroll_file_ = effective_scroll_y;
      }
    } else {
      // Manual scroll mode: use the scroll_y_ value directly
      effective_scroll_y = is_main_logger ? scroll_main_ : scroll_file_;
      // Clamp to valid range for this log section
      const int max_scroll =
          std::max(0, static_cast<int>(items.size()) - quarter_height);
      effective_scroll_y = std::clamp(effective_scroll_y, 0, max_scroll);
    }

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

    auto prefix_all = [](std::vector<std::string>& items,
                         std::vector<spdlog::level::level_enum>& levels) {
      // Add start-of-log marker at the beginning
      if (!items.empty()) {
        items.insert(items.begin(), "[START OF LOG]");
        levels.insert(levels.begin(), spdlog::level::info);
      }
      // Add end-of-log marker at the end
      if (!items.empty()) {
        items.emplace_back("[END OF LOG]");
        levels.emplace_back(spdlog::level::info);
      }
    };

    prefix_all(main_log_items_, main_log_levels_);
    prefix_all(file_log_items_, file_log_levels_);
  }

  std::function<int()> window_height_provider_;
  std::vector<std::string> main_log_items_;
  std::vector<spdlog::level::level_enum> main_log_levels_;

  std::vector<std::string> file_log_items_;
  std::vector<spdlog::level::level_enum> file_log_levels_;

  int scroll_x_ = 0;
  int scroll_main_ = 0;
  int scroll_file_ = 0;
  int auto_scroll_main_ = 0;
  int auto_scroll_file_ = 0;
  bool auto_mode_ = true;
};

}  // namespace

Component MakeLogPanel(std::function<int()> window_height_provider) {
  return std::make_shared<LogPanelBase>(std::move(window_height_provider));
}

void AutoFollowLogBottom(ftxui::Component& log_panel, int viewport_height) {
  // Cast the component to LogPanelBase to call the method
  if (auto* panel = dynamic_cast<LogPanelBase*>(log_panel.get())) {
    panel->AutoFollowBottom(viewport_height);
  }
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
