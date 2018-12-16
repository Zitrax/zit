// -*- mode:c++; c-basic-offset : 2; -*-
#include "sha1.h"

#include <algorithm>
#include <iterator>
#include <string>

#include <openssl/sha.h>

using namespace std;

namespace zit {

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

// We can't get away here without the reinterpret casts as long as we work with
// std::strings. Using another library would work (but they would still do the
// cast internally).
sha1 sha1::calculate(const std::string& data) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.length(),
           hash) == nullptr) {
    throw runtime_error("SHA1 calculation failed");
  }
  sha1 ret;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  zit::fill(ret, hash);
  return ret;
}

}  // namespace zit
