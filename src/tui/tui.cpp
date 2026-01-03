#include "controller.hpp"
#include "tui_logger.hpp"

#include "../arg_parser.hpp"

int main(int argc, const char* argv[]) noexcept {
  // Just to initilize the main and file writer
  // loggers for tui usage (memory sinks)
  zit::tui::zit_logger();
  zit::tui::file_writer_logger();
  zit::tui::logger()->info("Starting zit TUI");

  zit::ArgParser parser("zit TUI");
  parser.add_option<bool>("--clean")
      .help("Skip resuming saved torrents");
  
  try {
    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    parser.parse(args);
  } catch (const std::exception& e) {
    zit::tui::logger()->error("Argument parsing failed: {}", e.what());
    return 1;
  }

  const bool clean_start = parser.get<bool>("--clean");

  zit::tui::TuiController controller(clean_start);
  return controller.Run();
}
