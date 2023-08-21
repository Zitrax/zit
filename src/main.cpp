#include <spdlog/common.h>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "arg_parser.hpp"
#include "file_writer.hpp"
#include "global_config.hpp"
#include "logger.hpp"
#include "torrent.hpp"

#ifndef WIN32
#include <csignal>
#endif  // !WIN32

using namespace std;

namespace {

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void print_exception(const exception& e, string::size_type level = 0) noexcept {
  try {
    zit::logger()->error("{}exception: {}", string(level, ' '), e.what());
    try {
      rethrow_if_nested(e);
    } catch (const exception& ex) {
      print_exception(ex, level + 1);
    }
  } catch (const exception& ex) {
    cerr << "Error in error handling: " << ex.what() << "\n";
  }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::function<void(int)> sigint_function;
void sigint_handler(int s) {
  sigint_function(s);
}

}  // namespace

// False positives
// NOLINTBEGIN(misc-const-correctness)

int main(int argc, const char* argv[]) noexcept {
  try {
    zit::ArgParser parser("Zit - torrent client");
    std::string torrent_file;
    int listening_port{0};
    std::string log_level;
    bool help = false;
    bool dump_torrent = false;
    bool dump_config = false;
    parser.add_help_option("--help,-h", "Print help", help);
    parser.add_option("--torrent", {}, "Torrent file to download", torrent_file,
                      true);
    parser.add_option("--listening-port,-p", {},
                      "Port listening on incoming connections", listening_port);
    parser.add_option<std::string>(
        "--log-level", "",
        "Log level (trace, debug, info, warning, error, critical, off)",
        log_level);
    parser.add_option("--dump-torrent", {},
                      "Dump info about specified .torrent file and exit",
                      dump_torrent);
    parser.add_option("--dump-config", {}, "Dump config to console",
                      dump_config);
    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    parser.parse(args);

    if (help) {
      std::cout << parser.usage();
      return 0;
    }

    if (!log_level.empty()) {
      const auto lvl = spdlog::level::from_str(log_level);
      if (lvl == spdlog::level::off && log_level != "off") {
        throw runtime_error("Unknown log level: " + log_level);
      }
      zit::logger()->set_level(lvl);
      zit::logger("file_writer")->set_level(lvl);
    }

    zit::logger()->trace("TEST");

    class CommandLineArgs : public zit::Config {
     public:
      explicit CommandLineArgs(const int& listening_port)
          : m_listening_port(listening_port) {}

      [[nodiscard]] int get(zit::IntSetting setting) const override {
        if (setting == zit::IntSetting::LISTENING_PORT && m_listening_port) {
          return m_listening_port;
        }
        return zit::SingletonDirectoryFileConfig::getInstance().get(setting);
      }

     private:
      const int& m_listening_port;
    };

    CommandLineArgs clargs{listening_port};

    zit::Torrent torrent(torrent_file, "", clargs);
    if (dump_torrent) {
      std::cout << torrent << "\n";
      return 0;
    }
    if (dump_config) {
      std::cout << torrent.config();
      return 0;
    }

    zit::FileWriterThread file_writer(torrent, [&](zit::Torrent& /*t*/) {
      zit::logger()->info(
          "Download completed. Continuing to seed. Press ctrl-c to stop.");
    });
    zit::logger()->info("\n{}", torrent);

    sigint_function = [&](int /*s*/) {
      zit::logger()->warn("CTRL-C pressed. Stopping torrent...");
      torrent.stop();
    };

#ifndef WIN32
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,misc-include-cleaner)
    struct sigaction sigIntHandler {};
    sigIntHandler.sa_handler = sigint_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);
    // NOLINTEND(cppcoreguidelines-pro-type-union-access,misc-include-cleaner)
#endif  // !WIN32

    torrent.start();
    torrent.run();
  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}

// NOLINTEND(misc-const-correctness)
