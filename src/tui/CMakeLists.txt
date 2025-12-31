add_executable(zit_tui tui.cpp)

find_package(ftxui CONFIG REQUIRED)
target_link_libraries(zit_tui PRIVATE ftxui::dom ftxui::screen ftxui::component)
