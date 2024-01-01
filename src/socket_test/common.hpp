#pragma once

#include <fmt/format.h>
#include <asio.hpp>

// NOLINTBEGIN(readability-convert-member-functions-to-static)

template <>
struct fmt::formatter<asio::ip::tcp::endpoint> {
  constexpr static auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const asio::ip::tcp::endpoint& value,
              format_context& context) const {
    return fmt::format_to(context.out(), "{}:{}", value.address().to_string(),
                          value.port());
  }
};

// NOLINTEND(readability-convert-member-functions-to-static)

namespace socket_test {

/**
 * Completion handler rethrowing instead of ignoring exceptions
 */
const auto rethrow = [](const std::exception_ptr& e) {
  if (e) {
    std::rethrow_exception(e);
  }
};

class ID {
 public:
  [[nodiscard]] auto id() const { return m_id; }

 private:
  // This is supposedly fixed in newer versions of clang-tidy (but not yet in
  // the one I use) See https://github.com/llvm/llvm-project/issues/47384
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static unsigned m_counter;
  unsigned m_id{m_counter++};
};

}  // namespace socket_test
