#include "arg_parser.hpp"
#include "file_writer.hpp"
#include "global_config.hpp"
#include "logger.hpp"
#include "torrent.hpp"

#include <asio/io_context.hpp>
#include <fmt/format.h>
#include <spdlog/common.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

#ifdef WIN32
std::function<BOOL(DWORD)> ctrl_function;
BOOL ctrl_handler(DWORD d) {
  return ctrl_function(d);
}
#else
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::function<void(int)> sigint_function;
void sigint_handler(int s) {
  sigint_function(s);
}
#endif

}  // namespace

// False positives
// NOLINTBEGIN(misc-const-correctness)

int main(int argc, const char* argv[]) noexcept {
  try {
    zit::ArgParser parser("Zit - torrent client");
    parser.add_option<bool>("--help")
        .aliases({"-h"})
        .help("Print help")
        .help_arg();
    parser.add_option<std::string>("--torrent")
        .help("Torrent file to download")
        .required()
        .multi();
    parser.add_option<int>("--listening-port")
        .aliases({"-p"})
        .default_value(0)
        .help("Port listening on incoming connections");
    parser.add_option<std::string>("--log-level")
        .default_value("")
        .help("Log level (trace, debug, info, warning, error, critical, off)");
    parser.add_option<std::string>("--log-prefix")
        .default_value("")
        .help("Prefix to add to all log messages (useful when running multiple instances)");
    parser.add_option<bool>("--dump-torrent")
        .help("Dump info about specified .torrent file and exit");
    parser.add_option<bool>("--dump-config").help("Dump config to console");
    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    parser.parse(args);

    if (parser.get<bool>("--help")) {
      std::cout << parser.usage();
      return 0;
    }

    const auto torrent_files = parser.get_multi<std::string>("--torrent");
    const auto listening_port = parser.get<int>("--listening-port");
    const auto log_level = parser.get<std::string>("--log-level");
    const auto log_prefix = parser.get<std::string>("--log-prefix");
    const auto dump_torrent = parser.get<bool>("--dump-torrent");
    const auto dump_config = parser.get<bool>("--dump-config");

    if (!log_prefix.empty()) {
      // Set pattern with prefix for all loggers
      // Default spdlog pattern is: [%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v
      // We'll add the prefix before the message
      const auto pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [" + log_prefix + "] %v";
      zit::logger()->set_pattern(pattern);
      zit::logger("file_writer")->set_pattern(pattern);
    }

    if (!log_level.empty()) {
      const auto lvl = spdlog::level::from_str(log_level);
      if (lvl == spdlog::level::off && log_level != "off") {
        throw runtime_error("Unknown log level: " + log_level);
      }
      zit::logger()->set_level(lvl);
      zit::logger("file_writer")->set_level(lvl);
    }

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

      [[nodiscard]] bool get(zit::BoolSetting setting) const override {
        return zit::SingletonDirectoryFileConfig::getInstance().get(setting);
      }

     private:
      const int& m_listening_port;
    };

    CommandLineArgs clargs{listening_port};

    asio::io_context io_context;

    std::vector<std::unique_ptr<zit::Torrent>> torrents;

    std::ranges::transform(torrent_files, std::back_inserter(torrents),
                           [&](const auto& torrent_file) {
                             return std::make_unique<zit::Torrent>(
                                 io_context, torrent_file, "", clargs);
                           });

    if (dump_torrent || dump_config) {
      for (const auto& torrent : torrents) {
        if (dump_torrent) {
          std::cout << *torrent << "\n";
        }
        if (dump_config) {
          std::cout << torrent->config() << "\n";
        }
      }
      return 0;
    }

    zit::FileWriterThread file_writer([](zit::Torrent& torrent) {
      zit::logger()->info(fmt::format(
          "Download completed of {}. Continuing to seed. Press ctrl-c to stop.",
          torrent.name()));
    });

    for (auto& torrent : torrents) {
      file_writer.register_torrent(*torrent);
      zit::logger()->info("\n{}", *torrent);
    }

    // FIXME: Should no longer need to create one thread per torrent

    std::vector<std::thread> torrent_threads;
    std::ranges::transform(torrents, std::back_inserter(torrent_threads),
                           [](auto& torrent) {
                             return std::thread([&]() {
                               try {
                                 torrent->start();
                                 torrent->run();
                               } catch (const std::exception& ex) {
                                 print_exception(ex);
                               }
                             });
                           });

#ifdef WIN32
    // Windows ctrl-handler
    // Note as mentioned in https://stackoverflow.com/q/7404163/11722
    // that this in contrast to Linux will run in a new thread, thus
    // we will just call stop on the torrents which should be thread
    // safe and then let main fall out when the join is done.

    ctrl_function = [&](DWORD fdwCtrlType) -> BOOL {
      if (fdwCtrlType == CTRL_C_EVENT) {
        zit::logger()->warn("CTRL-C pressed. Stopping torrent(s)...");
        for (auto& torrent : torrents) {
          torrent->stop();
        }
        return TRUE;
      }
      return FALSE;
    };

    // Register the Ctrl-C handler
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE)) {
      zit::logger()->error("Error: Failed to set Ctrl-C handler.");
      return 1;
    }

#else
    // Linux ctrl-c handler

    sigint_function = [&](int /*s*/) {
      zit::logger()->warn("CTRL-C pressed. Stopping torrent(s)...");
      for (const auto& torrent : torrents) {
        torrent->stop();
      }
      for (auto& torrent_thread : torrent_threads) {
        torrent_thread.join();
      }
      // Assume we are the only one calling it for now
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      std::exit(0);
    };

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,misc-include-cleaner)
    struct sigaction sigIntHandler {};
    sigIntHandler.sa_handler = sigint_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);
    // NOLINTEND(cppcoreguidelines-pro-type-union-access,misc-include-cleaner)
#endif  // !WIN32

    for (auto& torrent_thread : torrent_threads) {
      torrent_thread.join();
    }

  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}

// NOLINTEND(misc-const-correctness)
