#pragma once

#include <chrono>
#include <format>
#include <thread>

#include "logger.hpp"

namespace zit {

template <typename Callable>
concept BoolCallable = requires(Callable c) {
  { bool(c()) } -> std::convertible_to<bool>;
};

/**
 * Retry a callable function n number of times with an optional rate limiting
 * delay until the function returns a "truthy" value.
 *
 * @param callable The callable must return a value that is convertible to bool.
 *   If returning anything else you can just make sure a lambda satisfy this.
 * @param retries A number of retries to perform on failure > 0.
 * @param min_interval Do not call next retry until at least this interval has
 *   passed.
 */
inline auto retry_call(
    BoolCallable auto callable,
    unsigned retries = 1,
    std::chrono::milliseconds min_interval = std::chrono::milliseconds{0}) {
  if (retries == 0) {
    throw std::runtime_error("retry_call called with 0 retries");
  }

  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  TimePoint last_call;

  for (unsigned i = 0; i < retries; ++i) {
    last_call = Clock::now();
    if (auto ret = callable(); ret || i == retries - 1) {
      return ret;
    }
    const auto time_spent = Clock::now() - last_call;
    if (i < (retries - 1)) {
      if (time_spent < min_interval) {
        const auto wait = min_interval - time_spent;
        logger()->trace(
            "Waiting {} ms before next retry",
            std::chrono::duration_cast<std::chrono::milliseconds>(wait)
                .count());
        std::this_thread::sleep_for(wait);
      } else {
        logger()->trace("Retrying call");
      }
    }
  }

  throw std::runtime_error("retry_call should not end up here");
}

}  // namespace zit