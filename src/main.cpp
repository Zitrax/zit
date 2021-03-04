#include <filesystem>
#include <iostream>
#include <vector>
#include "arg_parser.h"
#include "bencode.h"
#include "file_writer.h"
#include "net.h"
#include "torrent.h"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

using namespace std;
using namespace bencode;

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
static void print_exception(const exception& e,
                            string::size_type level = 0) noexcept {
  try {
    spdlog::get("console")->error("{}exception: {}", string(level, ' '),
                                  e.what());
    try {
      rethrow_if_nested(e);
    } catch (const exception& e) {
      print_exception(e, level + 1);
    }
  } catch (const exception& e) {
    cerr << "Error in error handling: " << e.what() << "\n";
  }
}

int main(int argc, const char* argv[]) noexcept {
  try {
    auto console = spdlog::stdout_color_mt("console");

    zit::ArgParser parser("Zit - torrent client");
    std::string torrent_file;
    std::string log_level;
    bool help = false;
    parser.add_option<bool>("--help", false, "Print help", help);
    parser.add_option("--torrent", {}, "Torrent file to download", torrent_file,
                      true);
    parser.add_option<std::string>(
        "--log-level", "info"s,
        "Log level (trace, debug, info, warning, error, critical, off)",
        log_level);
    parser.parse(argc, argv);

    if (help) {
      std::cout << parser.usage();
      return 0;
    }

    const auto lvl = spdlog::level::from_str(log_level);
    if (lvl == spdlog::level::off && log_level != "off") {
      throw runtime_error("Unknown log level: " + log_level);
    }
    console->set_level(lvl);

    zit::Torrent torrent(torrent_file);
    zit::FileWriterThread file_writer(torrent, [&console](zit::Torrent& tor) {
      console->info("Download completed");
      for_each(tor.peers().begin(), tor.peers().end(),
               [](auto& peer) { peer->stop(); });
    });
    console->info("\n{}", torrent);
    torrent.start();
    torrent.run();
  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}
