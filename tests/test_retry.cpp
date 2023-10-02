#include "gtest/gtest.h"
#include "retry.hpp"

#include <chrono>
#include <stdexcept>

using namespace zit;
using namespace std::chrono_literals;

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

TEST(retry, simple) {
  EXPECT_TRUE(retry_call([] { return true; }));
}

TEST(retry, exhaust_retries) {
  EXPECT_FALSE(retry_call([] { return false; }));
}

TEST(retry, retry_count) {
  unsigned count = 0;
  constexpr auto retries = 5;
  retry_call([&] { return ++count == retries; }, retries);
  EXPECT_EQ(count, retries);
}

TEST(retry, retry_interval) {
  const TimePoint start = Clock::now();
  unsigned count = 0;
  constexpr auto interval = 40ms;
  constexpr auto retries = 3;
  retry_call([&] { return ++count == retries; }, retries, interval);
  const auto time_spent = Clock::now() - start;
  EXPECT_EQ(count, retries);
  EXPECT_GT(time_spent, interval * (retries - 1))
      << "Waited "
      << std::chrono::duration_cast<std::chrono::milliseconds>(time_spent)
             .count()
      << " ms";
}

TEST(retry, retry_non_exhaustive) {
  unsigned count = 0;
  constexpr auto retries = 5;
  retry_call([&] { return ++count == retries - 1; }, retries);
  EXPECT_EQ(count, retries - 1);
}

TEST(retry, throw_exception) {
  unsigned count = 0;
  constexpr auto retries = 5;
  EXPECT_THROW(retry_call(
                   [&] {
                     if (++count == (retries - 2)) {
                       throw std::logic_error("nope");
                     } else {
                       return false;
                     }
                   },
                   retries),
               std::logic_error);
}

TEST(retry, retry_int) {
  unsigned count = 0;
  constexpr auto retries = 5;
  const auto ret = retry_call(
      [&]() -> int {
        if (++count == retries) {
          return 7;
        } else {
          return 0;
        }
      },
      retries);
  EXPECT_EQ(count, retries);
  EXPECT_EQ(ret, 7);
}

TEST(retry, retry_optional) {
  unsigned count = 0;
  constexpr auto retries = 5;
  const auto ret = retry_call(
      [&]() -> std::optional<int> {
        if (++count == retries) {
          return 7;
        } else {
          return {};
        }
      },
      retries);
  EXPECT_EQ(count, retries);
  EXPECT_EQ(ret, 7);
}
