#include <filesystem>
#include <iostream>
#include <vector>
#include "bencode.h"
#include "net.h"
#include "torrent.h"

using namespace std;
using namespace bencode;

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
void print_exception(const exception& e, int level = 0) {
  cerr << string(level, ' ') << "exception: " << e.what() << '\n';
  try {
    rethrow_if_nested(e);
  } catch (const exception& e) {
    print_exception(e, level + 1);
  } catch (...) {
  }
}

int main() {
  try {
    // Read .torrent file
    std::filesystem::path p(__FILE__);
    p.remove_filename();

    zit::Torrent torrent(p / ".." / "tests" / "data" / "test.torrent");
    cout << torrent << "\n";
    auto peers = torrent.start();

    for (const auto& peer : peers) {
      cout << peer << "\n";
    }

    peers[0].handshake(torrent.info_hash());

  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}
