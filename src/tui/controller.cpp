#include "controller.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include "view/components.hpp"

namespace zit::tui {

using namespace ftxui;

TuiController::TuiController()
    : screen_(ScreenInteractive::Fullscreen()) {
  BuildComponents();
  BindEvents();
}

int TuiController::Run() {
  screen_.Loop(main_renderer_);
  return 0;
}

void TuiController::BuildComponents() {
  MenuOption menu_option;
  menu_option.on_enter = [this] {
    if (!model_.empty()) {
      show_details_ = !show_details_;
    }
  };

  menu_component_ =
      Menu(&model_.menu_entries(), model_.mutable_selected_index(), menu_option);

  menu_renderer_ = Renderer(menu_component_, [this] {
    return view::RenderTorrentTable(model_, model_.selected_index());
  });

  detail_renderer_ = Renderer([this] {
    return view::RenderDetailPanel(model_, model_.selected_index());
  });

  main_container_ = Container::Vertical({menu_renderer_});

  main_renderer_ = Renderer(main_container_, [this] {
    auto content = menu_renderer_->Render() | flex;

    if (show_details_ && !model_.empty()) {
      content = vbox({
          menu_renderer_->Render() | flex,
          detail_renderer_->Render(),
      });
    }

    return content;
  });

  file_dialog_ = view::MakeFileDialog(
      [this](const std::filesystem::path& path) {
        AddPlaceholderTorrent(path);
      },
      [this] { open_dialog_ = false; });

  main_renderer_ |= Modal(file_dialog_, &open_dialog_);
}

void TuiController::BindEvents() {
  main_renderer_ |= CatchEvent([this](Event event) {
    if (event == Event::Character('q') || event == Event::Escape) {
      screen_.Exit();
      return true;
    }
    if (event == Event::Character('o')) {
      open_dialog_ = true;
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

void TuiController::AddPlaceholderTorrent(const std::filesystem::path& path) {
  model_.AddTorrent({path.filename().string(), "0.0%", "0 B", "0",
                     "0 B/s", "0 B/s"});
}

}  // namespace zit::tui
