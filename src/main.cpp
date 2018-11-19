#include <iostream>
#include <vector>
#include "bencode.h"
#include "net.h"

using namespace std;
using namespace bencode;

int main() {
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

  // Net
  zit::Net net;

  try {
    auto[headers, body] = net.http_get("uu.se", "/");
    cout << "=====HEADER=====\n"
         << headers << "\n=====BODY=====\n"
         << body << "\n";
  } catch (const exception& e) {
    cerr << "ERROR: '" << e.what() << "'\n";
  }
}
