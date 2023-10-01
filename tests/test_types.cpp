// -*- mode:c++; c-basic-offset : 2; -*-
#include "gtest/gtest.h"
#include "types.hpp"

using namespace zit;

TEST(types, from_big_endian) {
  bytes buf{12_b, 34_b, 56_b, 78_b};
  EXPECT_EQ(from_big_endian<uint32_t>(buf), 203569230);
}

TEST(types, big_endian_offset) {
  bytes buf{99_b, 12_b, 34_b, 56_b, 78_b};
  EXPECT_EQ(from_big_endian<uint32_t>(buf, 0), 1661739576);
  EXPECT_EQ(from_big_endian<uint32_t>(buf, 1), 203569230);
  EXPECT_THROW(from_big_endian<uint32_t>(buf, 2), std::out_of_range);
  EXPECT_THROW(
      from_big_endian<uint32_t>(buf, static_cast<bytes::size_type>(-1)),
      std::out_of_range);
  EXPECT_THROW(
      from_big_endian<uint32_t>(buf, static_cast<bytes::size_type>(-2)),
      std::out_of_range);
  EXPECT_THROW(
      from_big_endian<uint32_t>(buf, static_cast<bytes::size_type>(-3)),
      std::out_of_range);
  EXPECT_THROW(
      from_big_endian<uint32_t>(buf, static_cast<bytes::size_type>(-4)),
      std::out_of_range);
  EXPECT_THROW(
      from_big_endian<uint32_t>(buf, static_cast<bytes::size_type>(-5)),
      std::out_of_range);
}

TEST(types, there_and_back_i16) {
  {
    constexpr int16_t original{5188};
    const auto converted = to_big_endian<int16_t>(original);
    const auto back = from_big_endian<int16_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{56_b, 78_b};
    const auto converted = from_big_endian<int16_t>(original);
    const auto back = to_big_endian<int16_t>(converted);
    EXPECT_EQ(original, back);
  }
}

TEST(types, there_and_back_i32) {
  {
    constexpr int32_t original{1143018564};
    const auto converted = to_big_endian<int32_t>(original);
    const auto back = from_big_endian<int32_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{12_b, 34_b, 56_b, 78_b};
    const auto converted = from_big_endian<int32_t>(original);
    const auto back = to_big_endian<int32_t>(converted);
    EXPECT_EQ(original, back);
  }
}

TEST(types, there_and_back_i64) {
  {
    constexpr int64_t original{2311543152571323460};
    const auto converted = to_big_endian<int64_t>(original);
    const auto back = from_big_endian<int64_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{12_b, 34_b, 56_b, 78_b, 90_b, 12_b, 34_b, 56_b};
    const auto converted = from_big_endian<int64_t>(original);
    const auto back = to_big_endian<int64_t>(converted);
    EXPECT_EQ(original, back);
  }
}

TEST(types, there_and_back_u16) {
  {
    constexpr uint16_t original{43058};
    const auto converted = to_big_endian<uint16_t>(original);
    const auto back = from_big_endian<uint16_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{56_b, 78_b};
    const auto converted = from_big_endian<uint16_t>(original);
    const auto back = to_big_endian<uint16_t>(converted);
    EXPECT_EQ(original, back);
  }
}

TEST(types, there_and_back_u32) {
  {
    constexpr uint32_t original{3364137010};
    const auto converted = to_big_endian<uint32_t>(original);
    const auto back = from_big_endian<uint32_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{12_b, 34_b, 56_b, 78_b};
    const auto converted = from_big_endian<uint32_t>(original);
    const auto back = to_big_endian<uint32_t>(converted);
    EXPECT_EQ(original, back);
  }
}

TEST(types, there_and_back_u64) {
  {
    constexpr uint64_t original{12000008353440114738ULL};
    const auto converted = to_big_endian<uint64_t>(original);
    const auto back = from_big_endian<uint64_t>(converted);
    EXPECT_EQ(original, back);
  }

  {
    const bytes original{12_b, 34_b, 56_b, 78_b, 90_b, 12_b, 34_b, 56_b};
    const auto converted = from_big_endian<uint64_t>(original);
    const auto back = to_big_endian<uint64_t>(converted);
    EXPECT_EQ(original, back);
  }
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
  auto ret = to_big_endian<uint32_t>(1);
  EXPECT_EQ(ret.size(), 4);
  int sum = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    sum += numeric_cast<int>(ret[i]);
  }
  EXPECT_EQ(sum, 1);

  ret = to_big_endian<uint32_t>(300);
  EXPECT_EQ(ret.size(), 4);
  sum = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    sum += numeric_cast<int>(ret[i]);
  }
  EXPECT_EQ(sum, 45);  // 1 2C = 1 + 44
}
