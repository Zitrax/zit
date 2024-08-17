#include "gtest/gtest.h"
#include "logger.hpp"
#include "random.hpp"

#include <algorithm>

// Note - for 100% reproducibility we could fix the seed
// but for now it should not be a big problem. I am mostly
// just checking that the generator is not completely broken.

TEST(random, basic_value) {
  // Basic check that we don't throw
  EXPECT_NO_THROW(zit::random_value<int>());

  std::set<int> numbers;
  std::generate_n(std::inserter(numbers, numbers.begin()), 10,
                  [] { return zit::random_value<int>(); });

  // Just a basic non-scientific check that the numbers are not completely
  // unspread. This should basically almost always return 10.
  EXPECT_GT(numbers.size(), 5);
}

TEST(random, basic_value_in_range) {
  // Basic check that we don't throw
  int range_min{500};
  int range_max{550};
  EXPECT_NO_THROW(zit::random_value<int>(range_min, range_max));

  std::set<int> numbers;
  std::generate_n(std::inserter(numbers, numbers.begin()), 10,
                  [&] { return zit::random_value<int>(range_min, range_max); });

  // Verify that all numbers are in the specified range
  EXPECT_TRUE(std::ranges::all_of(
      numbers, [&](auto n) { return n >= range_min && n <= range_max; }));
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
