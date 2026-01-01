#include "components.hpp"
#include "../tui_logger.hpp"

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

Element RenderLogPanel() {
  constexpr int LAST_N_LINES{20};

  // Helper to convert log level to FTXUI color
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

  auto main_logger = zit::tui::zit_logger();
  auto sinks = main_logger->sinks();
  std::vector<spdlog::details::log_msg_buffer> log_messages;
  for (const auto& sink : sinks) {
    auto ringbuffer_sink =
        std::dynamic_pointer_cast<spdlog::sinks::ringbuffer_sink_mt>(sink);
    if (ringbuffer_sink) {
      const auto entries = ringbuffer_sink->last_raw(LAST_N_LINES);
      log_messages.insert(log_messages.end(), entries.begin(), entries.end());
    }
  }
  Elements log_elements;
  for (const auto& msg : log_messages) {
    auto level_str = std::string("[") + to_string_view(msg.level).data() + "]";
    auto payload_str = std::string(msg.payload.data(), msg.payload.size());
    log_elements.push_back(text(level_str + " " + payload_str) |
                           color(level_to_color(msg.level)));
  }

  auto file_logger = zit::tui::file_writer_logger();
  sinks = file_logger->sinks();
  std::vector<spdlog::details::log_msg_buffer> file_log_messages;
  for (const auto& sink : sinks) {
    auto ringbuffer_sink =
        std::dynamic_pointer_cast<spdlog::sinks::ringbuffer_sink_mt>(sink);
    if (ringbuffer_sink) {
      const auto entries = ringbuffer_sink->last_raw(LAST_N_LINES);
      file_log_messages.insert(file_log_messages.end(), entries.begin(),
                               entries.end());
    }
  }
  Elements file_log_elements;
  for (const auto& msg : file_log_messages) {
    auto level_str = std::string("[") + to_string_view(msg.level).data() + "]";
    auto payload_str = std::string(msg.payload.data(), msg.payload.size());
    file_log_elements.push_back(text(level_str + " " + payload_str) |
                                color(level_to_color(msg.level)));
  }

  return vbox({
             text("Main Logger:") | bold,
             separator(),
             vbox(std::move(log_elements)) | flex,
             separator(),
             text("File Writer Logger:") | bold,
             separator(),
             vbox(std::move(file_log_elements)) | flex,
         }) |
         border | flex;
}

}  // namespace zit::tui::view
