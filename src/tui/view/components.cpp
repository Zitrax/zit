#include "components.hpp"
#include "../tui_logger.hpp"

#include <algorithm>
#include <iostream>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

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

Component MakeFileDialog(std::function<void(const std::filesystem::path&)> open,
                         std::function<void()> close) {
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
            [open, close, &populate]() {
              std::filesystem::current_path(
                  std::filesystem::current_path().parent_path());
              populate = true;
            },
            TextOnlyButtonOption(Color::Blue)));
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
            TextOnlyButtonOption(Color::Green)));
      }
      container->Add(Button(
          "Cancel", [close] { close(); }, TextOnlyButtonOption(Color::Red)));
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
      const auto& torrent = model.torrents()[i];
      const bool is_selected = static_cast<int>(i) == selected_index;

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
             text("(More information will be added here later)") | dim | center,
         }) |
         border | flex;
}

class LogPanelBase : public ComponentBase {
 public:
  explicit LogPanelBase(std::function<int()> window_height_provider)
    : window_height_provider_(std::move(window_height_provider)) {
    auto level_to_color = [](spdlog::level::level_enum level) -> Color {
      switch (level) {
        case spdlog::level::trace:
          return Color::Grey50;
        case spdlog::level::debug:
          return Color::Cyan;
        case spdlog::level::info:
          return Color::Green;
        case spdlog::level::warn:
          return Color::Yellow;
        case spdlog::level::err:
          return Color::Red;
        case spdlog::level::critical:
          return Color::RedLight;
        case spdlog::level::off:
        case spdlog::level::n_levels:
          return Color::White;
      }
      return Color::White;
    };

    auto make_option = [level_to_color](
                           const std::vector<spdlog::level::level_enum>& levels) {
      auto option = MenuOption::Vertical();
      option.entries_option.transform =
          [level_to_color, &levels](const EntryState& state) {
            Color c = Color::White;
            if (state.index >= 0 &&
                static_cast<size_t>(state.index) < levels.size()) {
              c = level_to_color(levels[static_cast<size_t>(state.index)]);
            }
            Element e = text(state.label) | color(c);
            if (state.focused) e = e | inverted;
            return e;
          };
      return option;
    };

    main_menu_ = Menu(&main_log_items_, &main_selected_,
                      make_option(main_log_levels_));
    file_menu_ = Menu(&file_log_items_, &file_selected_,
                      make_option(file_log_levels_));

    Add(main_menu_);
    Add(file_menu_);
  }

  Element OnRender() override {
    UpdateLogs();

    const int window_height = window_height_provider_
                                  ? std::max(1, window_height_provider_())
                                  : 1;
    const int quarter_height = std::max(1, window_height / 4);
    auto scrollable_panel = [quarter_height](Element element) {
      return element | vscroll_indicator | frame |
             size(HEIGHT, EQUAL, quarter_height);
    };
    return vbox({
               text("Main Logger:") | bold,
               separator(),
               scrollable_panel(main_menu_->Render()),
               separator(),
               text("File Writer Logger:") | bold,
               separator(),
               scrollable_panel(file_menu_->Render()),
           }) |
           border | flex;
  }

 private:
  void UpdateLogs() {
    auto update_one = [](std::shared_ptr<spdlog::logger> logger,
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
            items.push_back(level_str + " " + payload_str);
            levels.push_back(msg.level);
          }
        }
      }
      return static_cast<int>(items.size());
    };

    const int main_count =
        update_one(zit::tui::zit_logger(), main_log_items_, main_log_levels_);
    const int file_count = update_one(zit::tui::file_writer_logger(),
                                      file_log_items_, file_log_levels_);

    main_selected_ = main_count > 0 ? main_count - 1 : 0;
    file_selected_ = file_count > 0 ? file_count - 1 : 0;
  }

  std::function<int()> window_height_provider_;
  std::vector<std::string> main_log_items_;
  std::vector<spdlog::level::level_enum> main_log_levels_;
  int main_selected_ = 0;
  Component main_menu_;

  std::vector<std::string> file_log_items_;
  std::vector<spdlog::level::level_enum> file_log_levels_;
  int file_selected_ = 0;
  Component file_menu_;
};

Component MakeLogPanel(std::function<int()> window_height_provider) {
  return std::make_shared<LogPanelBase>(std::move(window_height_provider));
}

}  // namespace zit::tui::view
