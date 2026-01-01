#include "controller.hpp"
#include "tui_logger.hpp"

int main(int /*argc*/, const char* /*argv*/[]) noexcept {
  // Just to initilize the main and file writer
  // loggers for tui usage (memory sinks)
  zit::tui::zit_logger();
  zit::tui::file_writer_logger();
  zit::tui::logger()->info("Starting zit TUI");

  zit::tui::TuiController controller;
  return controller.Run();
}
