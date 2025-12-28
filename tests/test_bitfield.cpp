// -*- mode:c++; c-basic-offset : 2; -*-
#include "bitfield.hpp"
#include "gtest/gtest.h"

#include <iostream>

using namespace zit;

TEST(Bitfield, construct) {
  Bitfield bf;

  bytes b{1_b, 40_b};
  Bitfield bf2(b);
}

// Ensure the bits are handled/read in the correct order
TEST(Bitfield, order) {
  bytes b{0xFF_b, 0xFE_b};
  Bitfield bf(b);

  EXPECT_TRUE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_TRUE(bf[3]);
  EXPECT_TRUE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);

  EXPECT_TRUE(bf[8]);
  EXPECT_TRUE(bf[9]);
  EXPECT_TRUE(bf[10]);
  EXPECT_TRUE(bf[11]);
  EXPECT_TRUE(bf[12]);
  EXPECT_TRUE(bf[13]);
  EXPECT_TRUE(bf[14]);
  EXPECT_FALSE(bf[15]);
}

TEST(Bitfield, single_byte_read_1) {
  bytes b{1_b};
  Bitfield bf(b);

  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_TRUE(bf[7]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW([[maybe_unused]] bool c = bf[8], std::out_of_range);
}

TEST(Bitfield, single_byte_read_2) {
  bytes b{5_b};
  Bitfield bf(b);

  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_TRUE(bf[7]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW([[maybe_unused]] bool c = bf[8], std::out_of_range);
}

TEST(Bitfield, multi_byte_read) {
  bytes b{7_b, 9_b};
  Bitfield bf(b);

  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);

  EXPECT_FALSE(bf[8]);
  EXPECT_FALSE(bf[9]);
  EXPECT_FALSE(bf[10]);
  EXPECT_FALSE(bf[11]);
  EXPECT_TRUE(bf[12]);
  EXPECT_FALSE(bf[13]);
  EXPECT_FALSE(bf[14]);
  EXPECT_TRUE(bf[15]);
  // Need to assign to trigger exception since
  // the use of proxy makes the throw lazy
  EXPECT_THROW([[maybe_unused]] bool c = bf[16], std::out_of_range);
}

TEST(Bitfield, single_byte_write) {
  Bitfield bf;
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

TEST(Bitfield, multi_byte_write) {
  Bitfield bf;
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

TEST(Bitfield, assign) {
  Bitfield bf1{bytes{0_b}};
  Bitfield bf2{bytes{0_b}};

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

TEST(Bitfield, next) {
  Bitfield bf{bytes{0_b}};
  EXPECT_EQ(bf.size(), 8);
  EXPECT_FALSE(bf.next(true));
  EXPECT_EQ(*bf.next(false), 0);

  bf[3] = true;
  EXPECT_EQ(*bf.next(true), 3);
  EXPECT_EQ(*bf.next(false), 0);

  bf = Bitfield();
  bf[100] = true;
  EXPECT_EQ(bf.size(), 104);  // 13 bytes
  EXPECT_EQ(*bf.next(true), 100);

  bf = Bitfield(bytes{255_b, 255_b, 255_b, 255_b, 255_b});
  EXPECT_EQ(bf.size(), 40);
  EXPECT_FALSE(bf.next(false));
  EXPECT_EQ(*bf.next(true), 0);
  bf[33] = false;
  EXPECT_EQ(*bf.next(false), 33);
  EXPECT_EQ(*bf.next(true), 0);

  bf = Bitfield();
  bf[2] = true;
  bf[4] = true;
  bf[44] = true;
  bf[80] = true;
  EXPECT_EQ(bf.next(true), 2);
  EXPECT_EQ(bf.next(true, 3), 4);
  EXPECT_EQ(bf.next(true, 5), 44);
  EXPECT_EQ(bf.next(true, 44), 44);
  EXPECT_EQ(bf.next(true, 45), 80);
  EXPECT_FALSE(bf.next(true, 81));

  bf = Bitfield();
  bf[14] = true;
  bf[16] = true;
  bf[18] = true;
  bf[20] = true;
  bf[22] = true;
  bf[24] = true;
  bf[25] = true;
  EXPECT_EQ(bf.next(true), 14);
  EXPECT_EQ(bf.next(true, 14), 14);
  EXPECT_EQ(bf.next(false, 14), 15);
  EXPECT_EQ(bf.next(true, 15), 16);
  EXPECT_EQ(bf.next(false, 15), 15);
  EXPECT_EQ(bf.next(true, 16), 16);
  EXPECT_EQ(bf.next(false, 16), 17);
  EXPECT_EQ(bf.next(true, 17), 18);
  EXPECT_EQ(bf.next(false, 17), 17);
  EXPECT_EQ(bf.next(true, 18), 18);
  EXPECT_EQ(bf.next(false, 18), 19);
  EXPECT_EQ(bf.next(true, 19), 20);
  EXPECT_EQ(bf.next(false, 19), 19);
  EXPECT_EQ(bf.next(true, 20), 20);
  EXPECT_EQ(bf.next(false, 20), 21);
  EXPECT_EQ(bf.next(true, 21), 22);
  EXPECT_EQ(bf.next(false, 21), 21);
  EXPECT_EQ(bf.next(true, 22), 22);
  EXPECT_EQ(bf.next(false, 22), 23);
  EXPECT_EQ(bf.next(true, 23), 24);
  EXPECT_EQ(bf.next(false, 23), 23);
  EXPECT_EQ(bf.next(true, 24), 24);
  EXPECT_EQ(bf.next(false, 24), 26);
  EXPECT_EQ(bf.next(true, 25), 25);
  EXPECT_EQ(bf.next(false, 25), 26);
  EXPECT_FALSE(bf.next(true, 26));
  EXPECT_EQ(bf.next(false, 26), 26);
  EXPECT_FALSE(bf.next(false, 26000));
  EXPECT_FALSE(bf.next(false, 26001));
}

TEST(Bitfield, subtraction) {
  Bitfield bf1(bytes{255_b});
  Bitfield bf2(bytes{0_b});
  auto ret = bf1 - bf2;
  EXPECT_TRUE(ret[0]);
  EXPECT_TRUE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_TRUE(ret[3]);
  EXPECT_TRUE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_TRUE(ret[7]);
  ret = bf2 - bf1;
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_FALSE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_FALSE(ret[7]);
  // All bit combinations in one byte 10 10 11 00
  bf1 = Bitfield(bytes{3_b});  // 00000011
  bf2 = Bitfield(bytes{5_b});  // 00000101
  ret = bf1 - bf2;
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_FALSE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_FALSE(ret[7]);
  ret = bf2 - bf1;
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_FALSE(ret[7]);
  // Different sizes
  bf1 = Bitfield(bytes{240_b, 10_b});  // 11110000 00001010
  bf2 = Bitfield(bytes{85_b});         // 01010101
  ret = bf1 - bf2;
  EXPECT_EQ(ret.size_bytes(), 1);
  EXPECT_TRUE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_FALSE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_FALSE(ret[7]);
  ret = bf2 - bf1;
  EXPECT_EQ(ret.size_bytes(), 1);
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_TRUE(ret[7]);
}

TEST(Bitfield, addition) {
  Bitfield bf1(bytes{255_b});
  Bitfield bf2(bytes{0_b});
  auto ret = bf1 + bf2;
  EXPECT_TRUE(ret[0]);
  EXPECT_TRUE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_TRUE(ret[3]);
  EXPECT_TRUE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_TRUE(ret[7]);
  ret = bf2 + bf1;
  EXPECT_TRUE(ret[0]);
  EXPECT_TRUE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_TRUE(ret[3]);
  EXPECT_TRUE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_TRUE(ret[7]);
  // All bit combinations in one byte 10 10 11 00
  bf1 = Bitfield(bytes{3_b});  // 00000011
  bf2 = Bitfield(bytes{5_b});  // 00000101
  ret = bf1 + bf2;
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_TRUE(ret[7]);
  ret = bf2 + bf1;
  EXPECT_FALSE(ret[0]);
  EXPECT_FALSE(ret[1]);
  EXPECT_FALSE(ret[2]);
  EXPECT_FALSE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_TRUE(ret[6]);
  EXPECT_TRUE(ret[7]);
  // Different sizes
  bf1 = Bitfield(bytes{240_b, 10_b});  // 11110000 00001010
  bf2 = Bitfield(bytes{85_b});         // 01010101
  ret = bf1 + bf2;
  EXPECT_EQ(ret.size_bytes(), 1);
  EXPECT_TRUE(ret[0]);
  EXPECT_TRUE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_TRUE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_TRUE(ret[7]);
  ret = bf2 + bf1;
  EXPECT_EQ(ret.size_bytes(), 1);
  EXPECT_TRUE(ret[0]);
  EXPECT_TRUE(ret[1]);
  EXPECT_TRUE(ret[2]);
  EXPECT_TRUE(ret[3]);
  EXPECT_FALSE(ret[4]);
  EXPECT_TRUE(ret[5]);
  EXPECT_FALSE(ret[6]);
  EXPECT_TRUE(ret[7]);
}

TEST(Bitfield, count) {
  Bitfield bf;
  EXPECT_EQ(bf.count(), 0);
  bf = Bitfield(bytes{240_b, 10_b});  // 11110000 00001010
  EXPECT_EQ(bf.count(), 6);
  bf[7] = true;
  EXPECT_EQ(bf.count(), 7);
}

TEST(Bitfield, const_bitfield) {
  Bitfield bf;
  bf[7] = true;
  EXPECT_TRUE(bf[7]);
  EXPECT_TRUE(bf.get(7));

  const Bitfield& cbf = bf;
  EXPECT_TRUE(cbf[7]);
}

TEST(Bitfield, fill) {
  Bitfield bf;

  // Pointless - but should be allowed and not crash
  bf.fill(0, true);

  // Should throw since we have no size
  EXPECT_THROW(bf.fill(1, true), std::invalid_argument);
  EXPECT_THROW(bf.fill(8, true), std::invalid_argument);
  EXPECT_THROW(bf.fill(9, true), std::invalid_argument);

  // Test single byte fill
  bf = Bitfield(8);
  bf.fill(1, true);
  EXPECT_EQ(bf.count(), 1);
  EXPECT_TRUE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);

  bf.fill(8, true);
  EXPECT_EQ(bf.count(), 8);
  EXPECT_TRUE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_TRUE(bf[3]);
  EXPECT_TRUE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);

  // Partial fill single byte
  bf = Bitfield(8);
  bf.fill(3, true, 3);
  EXPECT_EQ(bf.count(), 3);
  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_TRUE(bf[3]);
  EXPECT_TRUE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);

  // Partial fill with offset
  bf = Bitfield(8 * 3);
  // This should result in: 00000011 11111111 10000000
  bf.fill(11, true, 6);
  EXPECT_EQ(bf.count(), 11);
  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);
  EXPECT_TRUE(bf[8]);
  EXPECT_TRUE(bf[9]);
  EXPECT_TRUE(bf[10]);
  EXPECT_TRUE(bf[11]);
  EXPECT_TRUE(bf[12]);
  EXPECT_TRUE(bf[13]);
  EXPECT_TRUE(bf[14]);
  EXPECT_TRUE(bf[15]);
  EXPECT_TRUE(bf[16]);
  EXPECT_FALSE(bf[17]);
  EXPECT_FALSE(bf[18]);
  EXPECT_FALSE(bf[19]);
  EXPECT_FALSE(bf[20]);
  EXPECT_FALSE(bf[21]);
  EXPECT_FALSE(bf[22]);
  EXPECT_FALSE(bf[23]);

  // Reverse previous tests - fill with zeroes instead

  bf = Bitfield(8);
  bf.fill(8, true);
  bf.fill(1, false);
  EXPECT_EQ(bf.count(), 7);
  EXPECT_FALSE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_TRUE(bf[3]);
  EXPECT_TRUE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);

  bf.fill(8, false);
  EXPECT_EQ(bf.count(), 0);
  EXPECT_FALSE(bf[0]);
  EXPECT_FALSE(bf[1]);
  EXPECT_FALSE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);

  // Partial fill single byte
  bf = Bitfield(8);
  bf.fill(8, true);
  bf.fill(3, false, 3);
  EXPECT_EQ(bf.count(), 5);
  EXPECT_TRUE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_FALSE(bf[3]);
  EXPECT_FALSE(bf[4]);
  EXPECT_FALSE(bf[5]);
  EXPECT_TRUE(bf[6]);
  EXPECT_TRUE(bf[7]);

  // Partial fill with offset
  bf = Bitfield(8 * 3);
  bf.fill(24, true);
  bf.fill(11, false, 6);
  EXPECT_EQ(bf.count(), 13);
  EXPECT_TRUE(bf[0]);
  EXPECT_TRUE(bf[1]);
  EXPECT_TRUE(bf[2]);
  EXPECT_TRUE(bf[3]);
  EXPECT_TRUE(bf[4]);
  EXPECT_TRUE(bf[5]);
  EXPECT_FALSE(bf[6]);
  EXPECT_FALSE(bf[7]);
  EXPECT_FALSE(bf[8]);
  EXPECT_FALSE(bf[9]);
  EXPECT_FALSE(bf[10]);
  EXPECT_FALSE(bf[11]);
  EXPECT_FALSE(bf[12]);
  EXPECT_FALSE(bf[13]);
  EXPECT_FALSE(bf[14]);
  EXPECT_FALSE(bf[15]);
  EXPECT_FALSE(bf[16]);
  EXPECT_TRUE(bf[17]);
  EXPECT_TRUE(bf[18]);
  EXPECT_TRUE(bf[19]);
  EXPECT_TRUE(bf[20]);
  EXPECT_TRUE(bf[21]);
  EXPECT_TRUE(bf[22]);
  EXPECT_TRUE(bf[23]);
}

TEST(Bitfield, clear) {
  Bitfield bf(16);
  bf.fill(16, true);
  EXPECT_EQ(bf.count(), 16);
  bf.clear();
  EXPECT_EQ(bf.size_bytes(), 0);
}
