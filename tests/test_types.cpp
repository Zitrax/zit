// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "types.h"

using namespace zit;

TEST(types, big_endian) {
  bytes buf{12_b, 34_b, 56_b, 78_b};
  EXPECT_EQ(big_endian(buf), 203569230);
}

TEST(types, big_endian_offset) {
  bytes buf{99_b, 12_b, 34_b, 56_b, 78_b};
  EXPECT_EQ(big_endian(buf, 0), 1661739576);
  EXPECT_EQ(big_endian(buf, 1), 203569230);
  EXPECT_THROW(big_endian(buf, 2), std::out_of_range);
  EXPECT_THROW(big_endian(buf, static_cast<bytes::size_type>(-1)),
               std::out_of_range);
  EXPECT_THROW(big_endian(buf, static_cast<bytes::size_type>(-2)),
               std::out_of_range);
  EXPECT_THROW(big_endian(buf, static_cast<bytes::size_type>(-3)),
               std::out_of_range);
  EXPECT_THROW(big_endian(buf, static_cast<bytes::size_type>(-4)),
               std::out_of_range);
  EXPECT_THROW(big_endian(buf, static_cast<bytes::size_type>(-5)),
               std::out_of_range);
}

TEST(types, numeric_cast) {
  EXPECT_THROW(numeric_cast<unsigned int>(-1), std::out_of_range);
}
