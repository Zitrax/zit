// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "string_utils.h"

using namespace std;
using namespace zit;

TEST(string_utils, from_bytes) {
  bytes buffer{'F'_b, 'O'_b, 'O'_b, 'b'_b, 'a'_b, 'r'_b};
  EXPECT_EQ(from_bytes(buffer), "FOObar");
  EXPECT_EQ(from_bytes(buffer, 0), "FOObar");
  EXPECT_EQ(from_bytes(buffer, 1), "OObar");
  EXPECT_EQ(from_bytes(buffer, 1, 1), "");
  EXPECT_EQ(from_bytes(buffer, 1, 6), "OObar");
  EXPECT_EQ(from_bytes(buffer, 1, 0), "OObar");
  EXPECT_EQ(from_bytes(buffer, 6, 6), "");

  EXPECT_THROW(from_bytes(buffer, 1, 7), std::invalid_argument);
  EXPECT_THROW(from_bytes(buffer, 6, 5), std::invalid_argument);
}

TEST(string_utils, to_hex) {
  EXPECT_EQ(to_hex(""), "");
  EXPECT_EQ(to_hex("\x01\x02\x03\x04"), "01020304");
  EXPECT_EQ(to_hex("\x05\x06\x07\x08"), "05060708");
  EXPECT_EQ(to_hex("\x09\x0A\x0B\x0C"), "090A0B0C");
  EXPECT_EQ(to_hex("\x0D\x0E\x0F\x10"), "0D0E0F10");
}
