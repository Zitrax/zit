// -*- mode:c++; c-basic-offset : 2; -*-
#include "sha1.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include <openssl/sha.h>

#include "string_utils.h"

using namespace std;

namespace zit {

static_assert(SHA_DIGEST_LENGTH == SHA_LENGTH);

template <typename T>
static void fill(Sha1& dst, T* src) {
  copy_n(src, SHA_DIGEST_LENGTH, ::begin(dst));
}

Sha1::Sha1(const std::string& val) : array() {
  if (val.size() != SHA_DIGEST_LENGTH) {
    throw std::invalid_argument("sha1 size must be 20, was " +
                                to_string(val.size()));
  }

  zit::fill(*this, val.data());
}

Sha1::Sha1() : array() {
  this->fill(0);
}

std::string Sha1::str() const {
  return std::string(data(), SHA_LENGTH);
}

std::string Sha1::hex() const {
  return to_hex(str());
}

Sha1 Sha1::calculate(const unsigned char* src, size_t count) {
  Sha1 ret;
  auto dst = reinterpret_cast<unsigned char*>(ret.data());

  if (SHA1(src, count, dst) == nullptr) {
    throw runtime_error("SHA1 calculation failed");
  }

  return ret;
}

Sha1 Sha1::calculate_data(const std::string& data) {
  return calculate(reinterpret_cast<const unsigned char*>(data.data()),
                   data.size());
}

Sha1 Sha1::calculate_data(const bytes& data) {
  return calculate(reinterpret_cast<const unsigned char*>(data.data()),
                   data.size());
}

Sha1 Sha1::calculate_file(const std::filesystem::path& file) {
  // Important to open in binary mode due to line endings
  // differing on windows and linux.
  ifstream file_stream{file, ios_base::in | ios_base::binary};
  file_stream.exceptions(ifstream::badbit);

  if (!file_stream) {
    throw invalid_argument("No such file: "s + file.string());
  }

  SHA_CTX ctxt;
  SHA1_Init(&ctxt);
  const int BUFFER_SIZE = 1024;
  vector<char> buffer(BUFFER_SIZE, 0);

  while (file_stream.read(&buffer[0], BUFFER_SIZE)) {
    SHA1_Update(&ctxt, buffer.data(), BUFFER_SIZE);
  }

  // Remainder
  SHA1_Update(&ctxt, buffer.data(), numeric_cast<size_t>(file_stream.gcount()));

  Sha1 ret;
  SHA1_Final(reinterpret_cast<unsigned char*>(&ret[0]), &ctxt);
  return ret;
}

template <typename T>
Sha1 Sha1::fromBuffer(const T& buffer, typename T::size_type offset) {
  if (offset + SHA_LENGTH > buffer.size()) {
    throw invalid_argument("Buffer too small for extracting sha1");
  }
  Sha1 ret;
  copy_n(reinterpret_cast<const char*>(&buffer[offset]), SHA_LENGTH, &ret[0]);
  return ret;
}

// To keep the implementation in the .cpp file
template Sha1 Sha1::fromBuffer<string>(const string& buffer,
                                       string::size_type offset);
template Sha1 Sha1::fromBuffer<bytes>(const bytes& buffer,
                                      bytes::size_type offset);

}  // namespace zit
