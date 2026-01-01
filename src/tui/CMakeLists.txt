add_executable(zit_tui
    controller.cpp
    model.cpp
    tui.cpp
    view/components.cpp
)

find_package(ftxui CONFIG REQUIRED)
target_link_libraries(zit_tui PRIVATE
    ftxui::dom
    ftxui::screen
    ftxui::component
    zitlib
)
