// -*- mode:c++; c-basic-offset : 2; -*-
#include "arg_parser.hpp"

#include "spdlog/fmt/fmt.h"
#include "types.hpp"

using namespace std;

namespace zit {

template <typename T>
void ArgParser::add_option(const string& option,
                           const optional<T>& def,
                           const string& help,
                           T& dst,
                           bool required) {
  if (ranges::find_if(m_options, [&](const auto& a) {
        return a->option() == option;
      }) != m_options.end()) {
    throw runtime_error(fmt::format("Duplicate option '{}' added", option));
  }

  if (ranges::find_if(m_options, [&](const auto& a) {
        return a->dst() == &dst;
      }) != m_options.end()) {
    throw runtime_error(
        fmt::format("Duplicate value reference for '{}' added", option));
  }

  if (def) {
    dst = def.value();
  }
  auto argType = []() {
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
  };

  m_options.emplace_back(
      std::make_unique<Arg<T>>(option, help, argType(), dst, required));
}

void ArgParser::add_help_option(const std::string& option,
                                const std::string& help,
                                bool& dst) {
  add_option(option, {false}, help, dst, false);
  m_options.back()->set_help_arg(true);
}

template void ArgParser::add_option<bool>(const string&,
                                          const optional<bool>&,
                                          const string&,
                                          bool&,
                                          bool);
template void ArgParser::add_option<int>(const string&,
                                         const optional<int>&,
                                         const string&,
                                         int&,
                                         bool);
template void ArgParser::add_option<unsigned>(const string&,
                                              const optional<unsigned>&,
                                              const string&,
                                              unsigned&,
                                              bool);
template void ArgParser::add_option<float>(const string&,
                                           const optional<float>&,
                                           const string&,
                                           float&,
                                           bool);
template void ArgParser::add_option<string>(const string&,
                                            const optional<string>&,
                                            const string&,
                                            string&,
                                            bool);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
void ArgParser::parse(int argc, const char* argv[]) {
  if (m_parsed) {
    throw runtime_error("Options already parsed");
  }
  m_parsed = true;
  for (int i = 1; i < argc; i++) {
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
      if (i == argc - 1) {
        throw runtime_error(fmt::format("Missing value for {}", name));
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      const char* val = argv[i++ + 1];

      switch (arg->type()) {
        case Type::FLOAT: {
          float fval = std::strtof(val, nullptr);
          if (errno == ERANGE) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          as<float>(arg).set_dst(fval);
        } break;
        case Type::INT:
          try {
            as<int>(arg).set_dst(
                numeric_cast<int>(std::strtol(val, nullptr, 10)));
          } catch (const std::out_of_range&) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          break;
        case Type::UINT:
          try {
            as<unsigned>(arg).set_dst(
                numeric_cast<unsigned>(std::strtol(val, nullptr, 10)));
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
  auto nop = ranges::find_if(
      m_options, [](const auto& o) { return !o->provided() && o->required(); });
  if (nop != m_options.end()) {
    if (!ranges::any_of(m_options, [](const auto& o) {
          return o->help_arg() && o->provided();
        })) {
      throw runtime_error(
          fmt::format("Required option '{}' not provided", (*nop)->option()));
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
