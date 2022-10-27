// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "string_utils.hpp"

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

TEST(string_utils, to_lower) {
  EXPECT_EQ(to_lower(""), "");
  EXPECT_EQ(to_lower("a"), "a");
  EXPECT_EQ(to_lower("A"), "a");
  EXPECT_EQ(to_lower("AbCdE"), "abcde");
}

TEST(string_utils, bytesToHumanReadable) {
  EXPECT_EQ(bytesToHumanReadable(0), "0 B");
  EXPECT_EQ(bytesToHumanReadable(1LL << 0), "1 B");
  EXPECT_EQ(bytesToHumanReadable(1LL << 40), "1.00 TiB");
  EXPECT_EQ(bytesToHumanReadable(1LL << 30), "1.00 GiB");
  EXPECT_EQ(bytesToHumanReadable((1LL << 30) - 10000), "1023.99 MiB");
  EXPECT_EQ(bytesToHumanReadable(1LL << 20), "1.00 MiB");
  EXPECT_EQ(bytesToHumanReadable(1LL << 10), "1.00 KiB");
  EXPECT_EQ(bytesToHumanReadable((1LL << 40) + (1LL << 39)), "1.50 TiB");
  EXPECT_EQ(bytesToHumanReadable(1LL << 26), "64.00 MiB");
  EXPECT_EQ(bytesToHumanReadable(4'660'291), "4.44 MiB");

  EXPECT_EQ(bytesToHumanReadable(-1LL), "-1 B");
  EXPECT_EQ(bytesToHumanReadable(-(1LL << 40)), "-1.00 TiB");
}
