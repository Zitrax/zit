#pragma once

#include <filesystem>
#include <functional>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "../model.hpp"

namespace zit::tui::view {

ftxui::ButtonOption TextOnlyButtonOption(ftxui::Color color_active);

ftxui::Component MakeFileDialog(
    std::function<void(const std::filesystem::path&)> open,
    std::function<void()> close);

ftxui::Element RenderTorrentTable(const TorrentListModel& model,
                                  int selected_index);

ftxui::Element RenderDetailPanel(const TorrentListModel& model,
                                 int selected_index);

ftxui::Component MakeLogPanel(std::function<int()> window_height_provider);

}  // namespace zit::tui::view
