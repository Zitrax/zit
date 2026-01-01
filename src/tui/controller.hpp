#pragma once

#include <filesystem>
#include <memory>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "model.hpp"

namespace zit::tui {

class TuiController {
 public:
  TuiController();

  int Run();

 private:
  void BuildComponents();
  void BindEvents();
  void AddPlaceholderTorrent(const std::filesystem::path& path);

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
};

}  // namespace zit::tui
