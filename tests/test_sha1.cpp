#include "gtest/gtest.h"
#include "sha1.hpp"

using namespace zit;
namespace fs = std::filesystem;

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
  const auto data_dir = fs::path(DATA_DIR);

  // Non - existing file
  auto test_file = data_dir / "nope";
  EXPECT_THROW(auto _ = Sha1::calculateFile(test_file).hex(),
               std::invalid_argument);

  // Exactly 1 MiB ( n % 1024 = 0 )
  test_file = data_dir / "1MiB.dat";
  auto sha1 = Sha1::calculateFile(test_file).hex();
  EXPECT_EQ("3C1F02DFDF5306F8655F33A5830AD9542AD04567", sha1);

  // Exactly 1 MB ( n % 1024 != 0 )
  test_file = data_dir / "1MB.dat";
  sha1 = Sha1::calculateFile(test_file).hex();
  EXPECT_EQ("2ADC0A886DF8CA77925750E27BB9BBDFEAA30CAB", sha1);
}
