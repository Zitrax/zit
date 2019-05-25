#include "bencode.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string str(reinterpret_cast<const char*>(data), size);
  try {
    bencode::decode(str);
  } catch (const std::invalid_argument&) {
  }
  return 0;
}
