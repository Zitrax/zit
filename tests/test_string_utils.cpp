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
