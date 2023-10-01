#include "gtest/gtest.h"
#include "random.hpp"

TEST(random, basic) {
  // Basic check that we don't throw
  EXPECT_NO_THROW(zit::random_value<int>());

  std::set<int> numbers;
  std::generate_n(std::inserter(numbers, numbers.begin()), 10,
                  zit::random_value<int>);

  // Just a basic non-scientific check that the numbers are not completely
  // unspread. This should basically almost always return 10.
  EXPECT_GT(numbers.size(), 5);
}
