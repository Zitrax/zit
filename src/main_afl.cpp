#include <fstream>
#include <iostream>
#include "bencode.hpp"

int main(int argc, char** argv) {
  if (argc > 1) {
    std::ifstream fin;
    fin.open(argv[1]);  // NOLINT
    // FIXME: Read from file
    return 1;
  }

  // FIXME: check __AFL_LOOP
  for (std::string line; std::getline(std::cin, line);) {
    try {
      bencode::decode(line);
    } catch (const std::exception&) {
      // This is fine
    }
  }

  return 0;
}
