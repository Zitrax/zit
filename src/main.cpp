#include <iostream>
#include <vector>
#include "bencode.h"

using namespace std;
using namespace bencode;

int main() {
  cout << encode(4) << "\n";

  // List
  auto l = std::vector<ElmPtr>{};
  l.push_back(Element::build(1));
  l.push_back(Element::build("foo"));
  l.push_back(Element::build(1U));
  cout << encode(l) << "\n";

  // Dict
  auto m = BencodeMap();
  m["cow"] = Element::build("moo");
  m["spam"] = Element::build("eggs");
  cout << encode(m) << "\n";
}
