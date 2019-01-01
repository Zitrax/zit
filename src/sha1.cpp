// -*- mode:c++; c-basic-offset : 2; -*-
#include "sha1.h"

#include <algorithm>
#include <iterator>
#include <string>

#include <openssl/sha.h>

using namespace std;

namespace zit {

static_assert(SHA_DIGEST_LENGTH == SHA_LENGTH);

template <typename T>
static void fill(sha1& dst, T* src) {
  copy_n(src, SHA_DIGEST_LENGTH, ::begin(dst));
}

sha1::sha1(const std::string& val) : array() {
  if (val.size() != SHA_DIGEST_LENGTH) {
    throw std::invalid_argument("sha1 size must be 20, was " +
                                to_string(val.size()));
  }

  zit::fill(*this, val.data());
}

sha1::sha1() : array() {
  this->fill(0);
}

std::string sha1::str() const {
  return std::string(data(), SHA_LENGTH);
}

// We can't get away here without the reinterpret casts as long as we work with
// std::strings. Using another library would work (but they would still do the
// cast internally).
sha1 sha1::calculate(const std::string& data) {
  auto src = reinterpret_cast<const unsigned char*>(data.data());

  sha1 ret;
  auto dst = reinterpret_cast<unsigned char*>(ret.data());

  if (SHA1(src, data.length(), dst) == nullptr) {
    throw runtime_error("SHA1 calculation failed");
  }

  return ret;
}

sha1 sha1::from_bytes(const bytes& buffer, bytes::size_type offset) {
  if (offset + SHA_LENGTH > buffer.size()) {
    throw invalid_argument("Buffer too small for extracting sha1");
  }
  sha1 ret;
  copy_n(reinterpret_cast<const char*>(&buffer[offset]), SHA_LENGTH, &ret[0]);
  return ret;
}

}  // namespace zit
