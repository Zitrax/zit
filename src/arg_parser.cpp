// -*- mode:c++; c-basic-offset : 2; -*-
#include "arg_parser.hpp"

#include "string_utils.hpp"
#include "types.hpp"

#include <bits/basic_string.h>
#include <fmt/core.h>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

namespace zit {

template <typename T>
void ArgParser::add_option(const string& options,
                           const optional<T>& def,
                           const string& help,
                           T& dst,
                           bool required,
                           bool help_option) {
  if (def) {
    dst = def.value();
  }
  const auto argType = [] {
    if constexpr (std::is_same_v<T, bool>) {
      return Type::BOOL;
    } else if (std::is_same_v<T, float>) {
      return Type::FLOAT;
    } else if (std::is_same_v<T, int>) {
      return Type::INT;
    } else if (std::is_same_v<T, unsigned>) {
      return Type::UINT;
    } else if (std::is_same_v<T, string>) {
      return Type::STRING;
    }
  }();

  bool alias = false;
  for (const auto& option : split(options, ",")) {
    if (ranges::find_if(m_options, [&](const auto& a) {
          return a->option() == option;
        }) != m_options.end()) {
      throw runtime_error(fmt::format("Duplicate option '{}' added", option));
    }

    if (!alias) {
      if (ranges::find_if(m_options, [&](const auto& a) {
            return a->dst() == &dst;
          }) != m_options.end()) {
        throw runtime_error(
            fmt::format("Duplicate value reference for '{}' added", option));
      }
    }

    m_options.emplace_back(std::make_unique<Arg<T>>(option, alias ? "〃" : help,
                                                    argType, dst, required));
    m_options.back()->set_help_arg(help_option);
    alias = true;
  }
}

void ArgParser::add_help_option(const std::string& options,
                                const std::string& help,
                                bool& dst) {
  add_option(options, {false}, help, dst, false, true);
}

template void ArgParser::add_option<bool>(const string&,
                                          const optional<bool>&,
                                          const string&,
                                          bool&,
                                          bool,
                                          bool);
template void ArgParser::add_option<int>(const string&,
                                         const optional<int>&,
                                         const string&,
                                         int&,
                                         bool,
                                         bool);
template void ArgParser::add_option<unsigned>(const string&,
                                              const optional<unsigned>&,
                                              const string&,
                                              unsigned&,
                                              bool,
                                              bool);
template void ArgParser::add_option<float>(const string&,
                                           const optional<float>&,
                                           const string&,
                                           float&,
                                           bool,
                                           bool);
template void ArgParser::add_option<string>(const string&,
                                            const optional<string>&,
                                            const string&,
                                            string&,
                                            bool,
                                            bool);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
void ArgParser::parse(const std::vector<std::string>& argv) {
  if (m_parsed) {
    throw runtime_error("Options already parsed");
  }
  m_parsed = true;
  for (size_t i = 1; i < argv.size(); i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::string_view name = argv[i];
    auto m = ranges::find_if(
        m_options, [&](const auto& a) { return a->option() == name; });
    if (m == m_options.end()) {
      throw runtime_error(fmt::format("Unknown argument: {}", name));
    }
    auto& arg = *m;

    if (arg->type() == Type::BOOL) {
      as<bool>(arg).set_dst(true);
    } else {
      // Read next arg
      if (i == argv.size() - 1) {
        throw runtime_error(fmt::format("Missing value for {}", name));
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      const auto& val = argv[i++ + 1];

      switch (arg->type()) {
        case Type::FLOAT: {
          const float fval = std::stof(val, nullptr);
          if (errno == ERANGE) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          as<float>(arg).set_dst(fval);
        } break;
        case Type::INT:
          try {
            as<int>(arg).set_dst(
                numeric_cast<int>(std::stol(val, nullptr, 10)));
          } catch (const std::out_of_range&) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          break;
        case Type::UINT:
          try {
            as<unsigned>(arg).set_dst(
                numeric_cast<unsigned>(std::stol(val, nullptr, 10)));
          } catch (const std::out_of_range&) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          break;
        case Type::STRING:
          as<string>(arg).set_dst(val);
          break;
        case Type::BOOL:
          throw runtime_error("Invalid option type");
      }
    }
    arg->set_provided(true);
  }

  // Check if all options got values
  auto missing_required_arg = ranges::find_if(
      m_options, [](const auto& o) { return !o->provided() && o->required(); });
  if (missing_required_arg != m_options.end()) {
    if (ranges::none_of(m_options, [](const auto& o) {
          return o->help_arg() && o->provided();
        })) {
      throw runtime_error(fmt::format("Required option '{}' not provided",
                                      (*missing_required_arg)->option()));
    }
  }
}

std::string ArgParser::usage() {
  stringstream ss;
  ss << "Usage:\n\n" << m_desc << "\n\n";

  auto it = ranges::max_element(m_options, [](const auto& a, const auto& b) {
    return a->option().size() < b->option().size();
  });
  if (it == m_options.end()) {
    return ss.str();
  }
  const auto width = (*it)->option().size();

  for (const auto& option : m_options) {
    ss << fmt::format("  {:{}}    {} {}\n", option->option(), width,
                      option->help(), option->required() ? "(required)"s : ""s);
  }
  return ss.str();
}

}  // namespace zit
