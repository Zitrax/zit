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
