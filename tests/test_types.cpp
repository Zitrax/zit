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
  // ok conversions
  EXPECT_EQ(numeric_cast<uint8_t>(1), 1);
  EXPECT_EQ(numeric_cast<int8_t>(-1), -1);

  // out of range detection

  // signed <- signed
  EXPECT_THROW(numeric_cast<int8_t>(-129), std::out_of_range);
  // signed <- unsigned
  EXPECT_THROW(numeric_cast<int8_t>(128), std::out_of_range);
  // unsigned <- signed
  EXPECT_THROW(numeric_cast<uint8_t>(-1), std::out_of_range);
  // unsigned <- unsigned
  EXPECT_THROW(numeric_cast<uint8_t>(256), std::out_of_range);

  // float
  EXPECT_THROW(numeric_cast<float>(1E39), std::out_of_range);
}

TEST(types, to_big_endian) {
  auto ret = to_big_endian(1);
  EXPECT_EQ(ret.size(), 4);
  int sum = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    sum += numeric_cast<int>(ret[i]);
  }
  EXPECT_EQ(sum, 1);

  ret = to_big_endian(300);
  EXPECT_EQ(ret.size(), 4);
  sum = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    sum += numeric_cast<int>(ret[i]);
  }
  EXPECT_EQ(sum, 45);  // 1 2C = 1 + 44
}
