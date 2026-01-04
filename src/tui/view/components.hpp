#pragma once

#include <filesystem>
#include <functional>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "../model.hpp"

namespace zit::tui::view {

ftxui::ButtonOption TextOnlyButtonOption(ftxui::Color color_active);

ftxui::Component MakeFileDialog(
    const std::function<void(const std::filesystem::path&)>& open,
    const std::function<void()>& close);

ftxui::Component MakeHelpModal();

ftxui::Element RenderTorrentTable(const TorrentListModel& model,
                                  int selected_index);

ftxui::Element RenderDetailPanel(const TorrentListModel& model,
                                 int selected_index);

ftxui::Component MakeLogPanel(std::function<int()> window_height_provider);

// Auto-scroll log to bottom if user is not manually scrolling
void AutoFollowLogBottom(ftxui::Component& log_panel, int viewport_height);

}  // namespace zit::tui::view
