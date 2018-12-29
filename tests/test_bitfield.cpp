// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.h"
#include "gtest/gtest.h"

#include <iostream>

using namespace zit;

TEST(bitfield, construct) {
  bitfield bf;

  bytes b{1_b, 40_b};
  bitfield bf2(b);
}

TEST(bitfield, single_byte_read_1) {
  bytes b{1_b};
  bitfield bf(b);

  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW(bool b = bf[8], std::out_of_range);
}

TEST(bitfield, single_byte_read_2) {
  bytes b{5_b};
  bitfield bf(b);

  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW(bool b = bf[8], std::out_of_range);
}

TEST(bitfield, multi_byte_read) {
  bytes b{7_b, 9_b};
  bitfield bf(b);

  EXPECT_TRUE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
  EXPECT_TRUE(bf[8]);
  EXPECT_FALSE(bf[9]);
  EXPECT_FALSE(bf[10]);
  EXPECT_TRUE(bf[11]);
  EXPECT_FALSE(bf[12]);
  EXPECT_FALSE(bf[13]);
  EXPECT_FALSE(bf[14]);
  EXPECT_FALSE(bf[15]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW(bool b = bf[16], std::out_of_range);
}

TEST(bitfield, single_byte_write) {
  bitfield bf;
  bf[0] = true;
  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);

  bf[7] = true;
  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_TRUE(bf[7]);

  // Reset 7th bit to ensure we can clear bits
  bf[7] = false;
  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
}

TEST(bitfield, multi_byte_write) {
  bitfield bf;
  bf[0] = true;
  bf[8] = true;
  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
  EXPECT_TRUE(bf[8]);
  EXPECT_FALSE(bf[9]);
  EXPECT_FALSE(bf[10]);
  EXPECT_FALSE(bf[11]);
  EXPECT_FALSE(bf[12]);
  EXPECT_FALSE(bf[13]);
  EXPECT_FALSE(bf[14]);
  EXPECT_FALSE(bf[15]);
}

TEST(bitfield, assign) {
  bitfield bf1{bytes{0_b}};
  bitfield bf2{bytes{0_b}};

  EXPECT_FALSE(bf1[0]);
  EXPECT_FALSE(bf1[1]);
  EXPECT_FALSE(bf1[2]);
  EXPECT_FALSE(bf1[3]);
  EXPECT_FALSE(bf1[4]);
  EXPECT_FALSE(bf1[5]);
  EXPECT_FALSE(bf1[6]);
  EXPECT_FALSE(bf1[7]);

  EXPECT_FALSE(bf2[0]);
  EXPECT_FALSE(bf2[1]);
  EXPECT_FALSE(bf2[2]);
  EXPECT_FALSE(bf2[3]);
  EXPECT_FALSE(bf2[4]);
  EXPECT_FALSE(bf2[5]);
  EXPECT_FALSE(bf2[6]);
  EXPECT_FALSE(bf2[7]);

  bf2[0] = true;
  bf1[1] = bf2[0];

  EXPECT_FALSE(bf1[0]);
  EXPECT_TRUE(bf1[1]);
  EXPECT_FALSE(bf1[2]);
  EXPECT_FALSE(bf1[3]);
  EXPECT_FALSE(bf1[4]);
  EXPECT_FALSE(bf1[5]);
  EXPECT_FALSE(bf1[6]);
  EXPECT_FALSE(bf1[7]);

  EXPECT_TRUE(bf2[0]);
  EXPECT_FALSE(bf2[1]);
  EXPECT_FALSE(bf2[2]);
  EXPECT_FALSE(bf2[3]);
  EXPECT_FALSE(bf2[4]);
  EXPECT_FALSE(bf2[5]);
  EXPECT_FALSE(bf2[6]);
  EXPECT_FALSE(bf2[7]);
}
