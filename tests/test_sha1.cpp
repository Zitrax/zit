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

TEST(sha1, file) {
  std::filesystem::path p(__FILE__);

  // Exactly 1 MiB ( n % 1024 = 0 )
  auto test_file = p.parent_path() / "data" / "1MiB.dat";
  auto sha1 = Sha1::calculate(test_file).hex();
  EXPECT_EQ("3C1F02DFDF5306F8655F33A5830AD9542AD04567", sha1);

  // Exactly 1 MB ( n % 1024 != 0 )
  test_file = p.parent_path() / "data" / "1MB.dat";
  sha1 = Sha1::calculate(test_file).hex();
  EXPECT_EQ("2ADC0A886DF8CA77925750E27BB9BBDFEAA30CAB", sha1);
}
