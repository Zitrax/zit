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
using namespace std::placeholders;
using namespace bencode;

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void print_exception(const exception& e, string::size_type level = 0) {
  spdlog::get("console")->error("{}exception: {}", string(level, ' '),
                                e.what());
  try {
    rethrow_if_nested(e);
  } catch (const exception& e) {
    print_exception(e, level + 1);
  } catch (...) {
  }
}

int main() {
  try {
    auto console = spdlog::stdout_color_mt("console");
    console->set_level(spdlog::level::info);

    zit::FileWriterThread file_writer;

    // Read .torrent file
    std::filesystem::path p(__FILE__);
    p.remove_filename();

    // zit::Torrent torrent(p / ".." / "tests" / "data" / "test.torrent");
    zit::Torrent torrent(p / ".." / "random.torrent");
    torrent.set_piece_callback(
        bind(&zit::FileWriter::add, &file_writer.get(), _1, _2));
    console->info("\n{}", torrent);
    auto peers = torrent.start();

    for (const auto& peer : peers) {
      console->info("\n{}", peer);
    }

    for (auto& peer : peers) {
      if (!(peer.url().host() == "127.0.0.1" &&
            peer.url().port() == peer.port())) {
        peer.handshake(torrent.info_hash());
        break;
      }
      console->debug("Ignored own peer");
    }

  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}
