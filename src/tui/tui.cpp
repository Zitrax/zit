#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace ftxui;

// Struct to hold torrent information
struct TorrentInfo {
  std::string name;
  std::string complete;
  std::string size;
  std::string peers;
  std::string down_speed;
  std::string up_speed;
};

namespace {

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

Component FileDialog(std::function<void(const std::filesystem::path&)> open,
                     std::function<void()> close) {
  auto container = Container::Vertical({});

  auto renderer = Renderer(container, [container, open, close,
                                       populate = true]() mutable {
    if (populate) {
      std::cerr << "Populating file dialog\n";
      container->DetachAllChildren();

      // If not at the root add ".."
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
                // Repopulate with new directory
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

}  // namespace

int main(int /*argc*/, const char* /*argv*/[]) noexcept {
  std::vector<TorrentInfo> torrents;
  int selected = 0;
  std::vector<std::string> menu_entries;

  auto update_menu_entries = [&] {
    menu_entries.clear();
    if (torrents.empty()) {
      // Provide a focusable entry so global shortcuts keep working.
      menu_entries.emplace_back("No torrents. Press 'o' to add one.");
      selected = 0;
      return;
    }
    menu_entries.reserve(torrents.size());
    for (const auto& torrent : torrents) {
      menu_entries.push_back(torrent.name);
    }
    if (selected < 0) {
      selected = 0;
    }
    if (selected >= static_cast<int>(menu_entries.size())) {
      selected = static_cast<int>(menu_entries.size()) - 1;
    }
  };

  auto add_torrent = [&](const TorrentInfo& info) {
    torrents.push_back(info);
    update_menu_entries();
  };

  update_menu_entries();

  // State variables
  bool show_details = false;
  bool open_dialog = false;

  auto screen = ScreenInteractive::Fullscreen();

  // Create menu with custom rendering
  MenuOption menu_option;
  menu_option.on_enter = [&] {
    if (!torrents.empty()) {
      show_details = !show_details;
    }
  };

  auto menu = Menu(&menu_entries, &selected, menu_option);

  // Custom renderer for the menu to display as a table
  auto menu_renderer = Renderer(menu, [&] {
    Elements rows;

    // Header row
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

    if (torrents.empty()) {
      rows.push_back(text("No torrents. Press 'o' to add one.") | center |
                     focus);
    } else {
      // Data rows
      for (size_t i = 0; i < torrents.size(); ++i) {
        const auto& torrent = torrents[i];
        bool is_selected = (static_cast<int>(i) == selected);

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
  });

  // Detail view component
  auto detail_view = Renderer([&] {
    if (torrents.empty()) {
      return vbox({
                 separator(),
                 text("No torrent selected") | center,
             }) |
             border | flex;
    }
    return vbox({
               separator(),
               text("Details for: " +
                    torrents[static_cast<size_t>(selected)].name) |
                   bold | center,
               text("(More information will be added here later)") | dim |
                   center,
           }) |
           border | flex;
  });

  // Main layout combining menu and detail view
  auto main_container = Container::Vertical({
      menu_renderer,
  });

  auto main_renderer = Renderer(main_container, [&] {
    auto content = menu_renderer->Render() | flex;

    if (show_details && !torrents.empty()) {
      content = vbox({
          menu_renderer->Render() | flex,
          detail_view->Render(),
      });
    }

    return content;
  });

  // Add keyboard event handling
  main_renderer |= CatchEvent([&](Event event) {
    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      return true;
    }
    if (event == Event::Character('o')) {
      open_dialog = true;
      return true;
    }
    if (event == Event::Return) {
      if (!torrents.empty()) {
        show_details = !show_details;
      }
      return true;
    }
    return false;
  });

  auto file_dialog = FileDialog(
      [&add_torrent](const std::filesystem::path& path) {
        add_torrent(
            {path.filename().string(), "0.0%", "0 B", "0", "0 B/s", "0 B/s"});
      },
      [&open_dialog] { open_dialog = false; });

  main_renderer |= Modal(file_dialog, &open_dialog);

  // Run the interactive event loop
  screen.Loop(main_renderer);

  return 0;
}
