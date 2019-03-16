#include "gtest/gtest.h"
#include "sha1.h"

using namespace zit;

TEST(sha1, equality) {
  Sha1 a, b;
  EXPECT_EQ(a, b);

  a = Sha1("aaaaaaaaaaaaaaaaaaaa");
  EXPECT_NE(a, b);

  b = Sha1("bbbbbbbbbbbbbbbbbbbb");
  EXPECT_NE(a, b);

  b = Sha1("aaaaaaaaaaaaaaaaaaaa");
  EXPECT_EQ(a, b);
}
