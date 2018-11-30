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
    cout << encode(4) << "\n";

    // List
    auto l = BeList{};
    l.push_back(Element::build(1));
    l.push_back(Element::build("foo"));
    l.push_back(Element::build(1U));
    cout << encode(l) << "\n";

    // Dict
    auto m = BeDict();
    m["cow"] = Element::build("moo");
    m["spam"] = Element::build("eggs");
    cout << encode(m) << "\n";

    // Read .torrent file
    std::filesystem::path p(__FILE__);
    p.remove_filename();
    zit::Torrent torrent(p / ".." / "tests" / "data" / "test.torrent");

    cout << torrent << "\n";

    zit::Url url(torrent.announce());
    cout << url;

    // Net
    zit::Net net;

    // FIXME: Add port
    auto[headers, body] = net.http_get(url.host(), url.path());
    cout << "=====HEADER=====\n"
         << headers << "\n=====BODY=====\n"
         << body << "\n";
  } catch (const exception& e) {
    print_exception(e);
    return 1;
  }
  return 0;
}
