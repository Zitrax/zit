#include <filesystem>
#include <iostream>
#include <vector>
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
void print_exception(const exception& e, string::size_type level = 0) noexcept {
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

int main() noexcept {
  try {
    auto console = spdlog::stdout_color_mt("console");
    console->set_level(spdlog::level::info);

    // Read .torrent file
    std::filesystem::path p(__FILE__);
    p.remove_filename();

    zit::Torrent torrent(p / ".." / "tests" / "data" / "test.torrent");
    // zit::Torrent torrent(p / ".." / "random.torrent");
    // zit::Torrent torrent(p / ".." / "test2.torrent");
    zit::FileWriterThread file_writer(
        torrent, [&console](zit::Torrent& torrent) {
          console->info("Download completed");
          for_each(torrent.peers().begin(), torrent.peers().end(),
                   [](auto& peer) { peer.stop(); });
        });
    console->info("\n{}", torrent);
    torrent.start();

    for (auto& peer : torrent.peers()) {
      console->info("\n{}", peer);
      peer.handshake(torrent.info_hash());
    }

    torrent.run();
  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}
