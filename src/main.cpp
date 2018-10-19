#include <iostream>
#include "bencode.h"

using namespace std;

int main() {
  cout << bencode::encode(4) << "\n";

  auto l = std::list<bencode::ElmPtr>{};
  l.push_back(bencode::Element::build(1));
  l.push_back(bencode::Element::build("foo"));
  l.push_back(bencode::Element::build(1U));
  cout << bencode::encode(l) << "\n";
}
