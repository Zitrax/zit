#include "gtest/gtest.h"
#include "logger.hpp"
#include "random.hpp"

// Note - for 100% reproducibility we could fix the seed
// but for now it should not be a big problem. I am mostly
// just checking that the generator is not completely broken.

TEST(random, basic_value) {
  // Basic check that we don't throw
  EXPECT_NO_THROW(zit::random_value<int>());

  std::set<int> numbers;
  std::generate_n(std::inserter(numbers, numbers.begin()), 10,
                  zit::random_value<int>);

  // Just a basic non-scientific check that the numbers are not completely
  // unspread. This should basically almost always return 10.
  EXPECT_GT(numbers.size(), 5);
}

// Basic sanity checks
TEST(random, basic_string) {
  const auto rs1 = zit::random_string(30);
  const auto rs2 = zit::random_string(30);
  zit::logger()->debug("rs1={} rs2={}", rs1, rs2);

  // Generated strings should contain more than one type of char
  std::set<char> chars;
  for (const auto c : rs1) {
    chars.insert(c);
  }
  // Chance of this happening: (1/62)^30 ~ 1 in 1E53
  EXPECT_GT(chars.size(), 1);

  // Two generated strings should not be equal
  EXPECT_NE(rs1, rs2);
}
