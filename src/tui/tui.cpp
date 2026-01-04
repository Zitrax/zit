#include "controller.hpp"
#include "tui_logger.hpp"

#include "../arg_parser.hpp"

#include <exception>
#include <iterator>
#include <iostream>
#include <vector>

int main(int argc, const char* argv[]) noexcept {
  try {
    // Just to initilize the main and file writer
    // loggers for tui usage (memory sinks)
    zit::tui::zit_logger();
    zit::tui::file_writer_logger();
    zit::tui::logger()->info("Starting zit TUI");

    zit::ArgParser parser("zit TUI");
    parser.add_option<bool>("--clean").help("Skip resuming saved torrents");

    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    parser.parse(args);

    const bool clean_start = parser.get<bool>("--clean");

    zit::tui::TuiController controller(clean_start);
    return controller.Run();
  } catch (const std::exception& ex) {
    std::cerr << "Fatal error: " << ex.what() << "\n";
    return 1;
  }
}
