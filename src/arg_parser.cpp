// -*- mode:c++; c-basic-offset : 2; -*-
#include "arg_parser.hpp"

#include "types.hpp"

#ifndef _MSC_VER
#include <bits/basic_string.h>
#endif  // !_MSC_VER
#include <fmt/core.h>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace zit {

ArgParser::ConstArgIterator ArgParser::find(const std::string& option) const {
  return std::ranges::find_if(m_options, [&](const auto& a) {
    return a->get_option() == option || a->get_aliases().contains(option);
  });
}

ArgParser::ArgIterator ArgParser::find(const std::string& option) {
  return std::ranges::find_if(m_options, [&](const auto& a) {
    return a->get_option() == option || a->get_aliases().contains(option);
  });
}

bool ArgParser::has_option(const std::string& option) const {
  return find(option) != m_options.end();
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
void ArgParser::parse(const std::vector<std::string>& argv) {
  if (m_parsed) {
    throw runtime_error("Options already parsed");
  }
  m_parsed = true;
  for (size_t i = 1; i < argv.size(); i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::string& name = argv[i];
    auto m = find(name);
    if (m == m_options.end()) {
      throw runtime_error(fmt::format("Unknown argument: {}", name));
    }
    auto& arg = *m;

    if (arg->get_type() == Type::BOOL) {
      as<bool>(arg).set_dst(true);
    } else {
      // Read next arg
      if (i == argv.size() - 1) {
        throw runtime_error(fmt::format("Missing value for {}", name));
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      const auto& val = argv[i++ + 1];

      switch (arg->get_type()) {
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
  auto missing_required_arg = ranges::find_if(m_options, [](const auto& o) {
    return !o->is_provided() && o->is_required();
  });
  if (missing_required_arg != m_options.end()) {
    if (ranges::none_of(m_options, [](const auto& o) {
          return o->is_help_arg() && o->is_provided();
        })) {
      throw runtime_error(fmt::format("Required option '{}' not provided",
                                      (*missing_required_arg)->get_option()));
    }
  }
}

std::string ArgParser::usage() {
  stringstream ss;
  ss << "Usage:\n\n" << m_desc << "\n\n";

  auto it = ranges::max_element(m_options, [](const auto& a, const auto& b) {
    return a->get_option().size() < b->get_option().size();
  });
  if (it == m_options.end()) {
    return ss.str();
  }
  const auto width = (*it)->get_option().size();

  for (const auto& option : m_options) {
    ss << fmt::format("  {:{}}    {} {}\n", option->get_option(), width,
                      option->get_help(),
                      option->is_required() ? "(required)"s : ""s);
  }
  return ss.str();
}

}  // namespace zit
